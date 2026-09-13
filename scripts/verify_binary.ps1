param(
    [switch]$MetadataOnly,
    [string]$Executable,
    [string]$OutputDirectory,
    [string]$Version,
    [ValidateSet("ci", "preview", "rc", "stable")][string]$Channel,
    [string]$Commit,
    [string]$BuildDateUtc,
    [string]$Repository,
    [string]$SourceRef,
    [uint32]$RunNumber,
    [switch]$RequireSignature,
    [string]$ExpectedSignerSubject
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-CompatibleImports([string]$dump) {
$forbiddenImports = @(
    "GetDpiForWindow",
    "AdjustWindowRectExForDpi",
    "GetSystemMetricsForDpi",
    "SetProcessDpiAwarenessContext",
    "SetThreadDpiAwarenessContext"
)
foreach ($name in $forbiddenImports) {
    if ($dump -match "(?m)^\s+(?:[0-9A-Fa-f]+\s+)?$([regex]::Escape($name))\s*$") {
        throw "Optional post-baseline API $name is imported directly."
    }
}
}

function Assert-Metadata {
    if ($Repository -cne "velocityeu/NetTools") {
        throw "Release metadata repository must be exactly velocityeu/NetTools."
    }
    if ($Commit -cnotmatch '^[0-9a-f]{40}$') {
        throw "Commit must be a complete lowercase 40-character SHA-1 object ID."
    }
    if ($RunNumber -lt 1 -or $RunNumber -gt 65535) {
        throw "RunNumber must fit the unsigned 16-bit PE build field without truncation."
    }
    $parsedDate = [DateTimeOffset]::MinValue
    if (-not [DateTimeOffset]::TryParseExact(
            $BuildDateUtc, "yyyy-MM-dd'T'HH:mm:ss'Z'",
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::AssumeUniversal,
            [ref]$parsedDate)) {
        throw "BuildDateUtc must be an exact UTC timestamp such as 2026-09-13T08:30:00Z."
    }

    $stablePattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
    $rcPattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-rc\.([1-9][0-9]*)$'
    $match = $null
    switch ($Channel) {
        "stable" {
            $match = [regex]::Match($Version, $stablePattern)
            if (-not $match.Success -or $SourceRef -cne "refs/tags/v$Version") {
                throw "Stable builds require an exact vX.Y.Z tag and matching version."
            }
        }
        "rc" {
            $match = [regex]::Match($Version, $rcPattern)
            if (-not $match.Success -or $SourceRef -cne "refs/tags/v$Version") {
                throw "Release candidates require an exact vX.Y.Z-rc.N tag and matching version."
            }
        }
        "preview" {
            $pattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-dev\.' +
                [regex]::Escape([string]$RunNumber) + '\+' +
                [regex]::Escape($Commit.Substring(0, 7)) + '$'
            $match = [regex]::Match($Version, $pattern)
            if (-not $match.Success -or $SourceRef -cne "refs/heads/main") {
                throw "Preview builds require main and version X.Y.Z-dev.RUN+SHA7."
            }
        }
        "ci" {
            $pattern = '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-ci\.' +
                [regex]::Escape([string]$RunNumber) + '\+' +
                [regex]::Escape($Commit.Substring(0, 7)) + '$'
            $match = [regex]::Match($Version, $pattern)
            if (-not $match.Success -or $SourceRef -cnotmatch '^refs/pull/[1-9][0-9]*/merge$') {
                throw "CI builds require a pull-request merge ref and version X.Y.Z-ci.RUN+SHA7."
            }
        }
    }

    foreach ($index in 1..3) {
        $component = [uint32]$match.Groups[$index].Value
        if ($component -gt 65535) {
            throw "Semantic version components must fit unsigned 16-bit PE version fields."
        }
    }
}

function Find-VisualStudioTool([string]$ToolName) {
    $locator = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $locator)) {
        throw "Visual Studio locator is unavailable."
    }
    $vsRoot = & $locator -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsRoot) {
        throw "Visual Studio C++ Build Tools are unavailable."
    }
    $toolset = Get-ChildItem -LiteralPath (Join-Path $vsRoot "VC\Tools\MSVC") -Directory |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1
    if (-not $toolset) {
        throw "MSVC toolset is unavailable."
    }
    $tool = Join-Path $toolset.FullName "bin\Hostx64\x64\$ToolName"
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "$ToolName is unavailable in the x64 MSVC toolset."
    }
    return $tool
}

function Find-WindowsSdkTool([string]$ToolName) {
    $binRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin"
    $tool = Get-ChildItem -Path (Join-Path $binRoot "*\x64\$ToolName") -File |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw "$ToolName is unavailable in the Windows SDK."
    }
    return $tool.FullName
}

Assert-Metadata
if ($MetadataOnly) {
    Write-Host "Release metadata is valid for $Channel $Version."
    exit 0
}

if (-not $Executable -or -not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable must name the built Velocity NetTools file."
}
if (-not $OutputDirectory) {
    throw "OutputDirectory is required."
}
if ($Channel -eq "stable" -and -not $RequireSignature) {
    throw "Stable publication requires Authenticode verification."
}
if ($RequireSignature -and -not $ExpectedSignerSubject) {
    throw "ExpectedSignerSubject is required when signature verification is mandatory."
}

$dumpbin = Find-VisualStudioTool "dumpbin.exe"
$mt = Find-WindowsSdkTool "mt.exe"
$dump = (& $dumpbin /headers /dependents /imports $Executable 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "DUMPBIN could not inspect the executable."
}
if ($dump -notmatch '(?i)\b8664\s+machine\s+\(x64\)') {
    throw "Executable is not an x64 PE image."
}
if ($dump -match '(?i)\b(?:VCRUNTIME|MSVCP)[0-9_]*D?\.DLL\b|\bUCRTBASED?\.DLL\b|\bapi-ms-win-crt-[a-z0-9-]+\.dll\b') {
    throw "Executable imports a separately installed Visual C++ runtime."
}
Assert-CompatibleImports $dump

$temporaryManifest = Join-Path ([IO.Path]::GetTempPath()) ("veu-manifest-" + [guid]::NewGuid() + ".xml")
try {
    & $mt -nologo "-inputresource:$Executable;#1" "-out:$temporaryManifest"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $temporaryManifest)) {
        throw "Embedded application manifest could not be extracted."
    }
    $manifest = Get-Content -Raw -LiteralPath $temporaryManifest
} finally {
    Remove-Item -LiteralPath $temporaryManifest -Force -ErrorAction SilentlyContinue
}
foreach ($required in @(
        'Microsoft.Windows.Common-Controls',
        '>true/pm<',
        '>PerMonitorV2, PerMonitor<',
        'requestedExecutionLevel level="asInvoker"')) {
    if ($manifest -cnotmatch [regex]::Escape($required)) {
        throw "Embedded manifest is missing required declaration: $required"
    }
}

$file = Get-Item -LiteralPath $Executable
$versionInfo = $file.VersionInfo
if ($versionInfo.CompanyName -cne "VEU") {
    throw "Embedded publisher identity is not VEU."
}
if ($versionInfo.ProductVersion -cne $Version -or $versionInfo.FileVersion -cne $Version) {
    throw "Embedded version does not match immutable build metadata $Version."
}
if ($versionInfo.OriginalFilename -cne "VelocityNetTools-x64.exe") {
    throw "Embedded original filename is not VelocityNetTools-x64.exe."
}

$signature = Get-AuthenticodeSignature -LiteralPath $Executable
$signatureStatus = $signature.Status.ToString()
if ($signatureStatus -notin @("Valid", "NotSigned")) {
    throw "Executable has an invalid Authenticode state: $signatureStatus."
}
if ($RequireSignature) {
    if ($signatureStatus -ne "Valid") {
        throw "Required Authenticode signature is absent or invalid."
    }
    if ($signature.SignerCertificate.Subject -cne $ExpectedSignerSubject) {
        throw "Authenticode publisher does not match the configured subject."
    }
    if (-not $signature.TimeStamperCertificate) {
        throw "Stable signature lacks a trusted timestamp certificate."
    }
}

$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) {
    if (Get-ChildItem -LiteralPath $output -Force | Select-Object -First 1) {
        throw "OutputDirectory must be empty so stale files cannot enter a release."
    }
} else {
    New-Item -ItemType Directory -Path $output | Out-Null
}

$releaseExe = Join-Path $output "VelocityNetTools-x64.exe"
Copy-Item -LiteralPath $Executable -Destination $releaseExe
$root = Split-Path $PSScriptRoot -Parent
Copy-Item -LiteralPath (Join-Path $root "LICENSE") -Destination (Join-Path $output "LICENSE.txt")
@"
Velocity NetTools third-party notices

The application uses Microsoft Windows system components supplied with the
operating system. No separately distributed third-party application runtime is
included.
"@ | Set-Content -LiteralPath (Join-Path $output "THIRD-PARTY-NOTICES.txt") -Encoding utf8NoBOM

$exeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $releaseExe).Hash.ToLowerInvariant()
$signing = if ($signatureStatus -eq "Valid") { "authenticode" } else { "unsigned" }
& python (Join-Path $PSScriptRoot "build_portable_zip.py") --directory $output --version $Version --signing $signing
if ($LASTEXITCODE -ne 0) { throw "Portable ZIP creation or verification failed." }
$releaseZip = Join-Path $output "VelocityNetTools-x64.zip"
$runnerImage = if ($env:ImageOS) { "$($env:ImageOS) $($env:ImageVersion)" } else { "local verification" }
$sdkVersion = (Get-Item -LiteralPath $mt).Directory.Parent.Name
$manifestObject = [ordered]@{
    schemaVersion = 1
    product = "Velocity NetTools"
    publisher = "VEU"
    repository = $Repository
    version = $Version
    channel = $Channel
    commit = $Commit
    sourceRef = $SourceRef
    buildDateUtc = $BuildDateUtc
    runNumber = $RunNumber
    platform = "windows-x64"
    minimumOS = "Windows 10 build 10240 or Windows Server 2016 Desktop Experience"
    filename = "VelocityNetTools-x64.exe"
    size = (Get-Item -LiteralPath $releaseExe).Length
    sha256 = $exeHash
    archive = [ordered]@{
        filename = "VelocityNetTools-x64.zip"
        size = (Get-Item -LiteralPath $releaseZip).Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $releaseZip).Hash.ToLowerInvariant()
    }
    signing = [ordered]@{
        policy = if ($RequireSignature) { "required" } else { "not-required-for-this-channel" }
        result = $signing
        subject = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
        timestamped = [bool]$signature.TimeStamperCertificate
    }
    toolchain = [ordered]@{
        runnerImage = $runnerImage
        msvcToolset = (Split-Path (Split-Path (Split-Path (Split-Path (Split-Path $dumpbin -Parent) -Parent) -Parent) -Parent) -Leaf)
        windowsSdk = $sdkVersion
    }
}
$manifestPath = Join-Path $output "release-manifest.json"
$manifestObject | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM

$importPath = Join-Path $output "pe-imports.txt"
$dump | Set-Content -LiteralPath $importPath -Encoding utf8NoBOM

$assetNames = @(
    "VelocityNetTools-x64.exe",
    "VelocityNetTools-x64.zip",
    "START-HERE.txt",
    "release-manifest.json",
    "LICENSE.txt",
    "THIRD-PARTY-NOTICES.txt",
    "pe-imports.txt"
)
$checksumLines = foreach ($name in $assetNames) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $output $name)).Hash.ToLowerInvariant()
    "$hash *$name"
}
$checksumLines | Set-Content -LiteralPath (Join-Path $output "SHA256SUMS.txt") -Encoding ascii

Write-Host "Verified and packaged Velocity NetTools $Version ($Channel) in $output."
