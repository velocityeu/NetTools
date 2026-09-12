$ErrorActionPreference = "Stop"
$script = Join-Path (Split-Path $PSScriptRoot -Parent) "scripts/verify_binary.ps1"
$failures = 0

function Invoke-Validation([string[]]$Arguments) {
    $output = & pwsh -NoLogo -NoProfile -File $script -MetadataOnly @Arguments 2>&1 | Out-String
    return @{ ExitCode = $LASTEXITCODE; Output = $output }
}

function Expect-Success([string]$Name, [string[]]$Arguments) {
    $result = Invoke-Validation $Arguments
    if ($result.ExitCode -ne 0) {
        Write-Error "FAIL $Name expected success: $($result.Output)" -ErrorAction Continue
        $script:failures++
    }
}

function Expect-Failure([string]$Name, [string[]]$Arguments) {
    $result = Invoke-Validation $Arguments
    if ($result.ExitCode -eq 0) {
        Write-Error "FAIL $Name expected rejection" -ErrorAction Continue
        $script:failures++
    }
}

$common = @(
    "-Commit", "0123456789abcdef0123456789abcdef01234567",
    "-BuildDateUtc", "2026-09-13T08:30:00Z",
    "-Repository", "velocityeu/NetTools",
    "-RunNumber", "42"
)

Expect-Success "stable tag" ($common + @("-Channel", "stable", "-Version", "1.2.3", "-SourceRef", "refs/tags/v1.2.3"))
Expect-Success "release candidate tag" ($common + @("-Channel", "rc", "-Version", "1.2.3-rc.4", "-SourceRef", "refs/tags/v1.2.3-rc.4"))
Expect-Success "main preview" ($common + @("-Channel", "preview", "-Version", "0.1.0-dev.42+0123456", "-SourceRef", "refs/heads/main"))
Expect-Success "pull request CI" ($common + @("-Channel", "ci", "-Version", "0.1.0-ci.42+0123456", "-SourceRef", "refs/pull/7/merge"))

Expect-Failure "stable tag mismatch" ($common + @("-Channel", "stable", "-Version", "1.2.3", "-SourceRef", "refs/tags/v1.2.4"))
Expect-Failure "leading-zero version" ($common + @("-Channel", "stable", "-Version", "01.2.3", "-SourceRef", "refs/tags/v01.2.3"))
Expect-Failure "PE component overflow" ($common + @("-Channel", "stable", "-Version", "65536.2.3", "-SourceRef", "refs/tags/v65536.2.3"))
Expect-Failure "PE build overflow" (@(
    "-Commit", "0123456789abcdef0123456789abcdef01234567",
    "-BuildDateUtc", "2026-09-13T08:30:00Z",
    "-Repository", "velocityeu/NetTools", "-RunNumber", "65536",
    "-Channel", "preview", "-Version", "0.1.0-dev.65536+0123456", "-SourceRef", "refs/heads/main"
))
Expect-Failure "preview outside main" ($common + @("-Channel", "preview", "-Version", "0.1.0-dev.42+0123456", "-SourceRef", "refs/heads/feature"))
Expect-Failure "wrong repository" (@(
    "-Commit", "0123456789abcdef0123456789abcdef01234567",
    "-BuildDateUtc", "2026-09-13T08:30:00Z", "-Repository", "someone/NetTools",
    "-RunNumber", "42", "-Channel", "stable", "-Version", "1.2.3", "-SourceRef", "refs/tags/v1.2.3"
))
Expect-Failure "abbreviated commit" (@(
    "-Commit", "0123456", "-BuildDateUtc", "2026-09-13T08:30:00Z",
    "-Repository", "velocityeu/NetTools", "-RunNumber", "42",
    "-Channel", "stable", "-Version", "1.2.3", "-SourceRef", "refs/tags/v1.2.3"
))

# Exercise the production import gate without executing packaging side effects.
$tokens=$null; $parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($script,[ref]$tokens,[ref]$parseErrors)
$importFunction=$ast.Find({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq "Assert-CompatibleImports"},$true)
if (-not $importFunction) { throw "Import compatibility gate is unavailable." }
. ([scriptblock]::Create($importFunction.Extent.Text))
foreach($sample in @("             6C3 GetDpiForWindow", "             2A AdjustWindowRectExForDpi", "    GetSystemMetricsForDpi")) {
    $rejected=$false
    try { Assert-CompatibleImports $sample } catch { $rejected=$true }
    if (-not $rejected) { Write-Error "FAIL forbidden native import accepted: $sample" -ErrorAction Continue; $failures++ }
}
try { Assert-CompatibleImports "             2DD GetProcAddress`n              7A CreateWindowExW" }
catch { Write-Error "FAIL baseline imports rejected" -ErrorAction Continue; $failures++ }
$workflowPath = Join-Path (Split-Path $PSScriptRoot -Parent) ".github/workflows/native.yml"
$workflow = Get-Content -Raw -LiteralPath $workflowPath
$requiredPins = @(
    "actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1",
    "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a",
    "actions/download-artifact@3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c",
    "actions/create-github-app-token@bcd2ba49218906704ab6c1aa796996da409d3eb1",
    "actions/attest@508db95dd578ae2727ebd6217d5ba78e4fbda05d"
)
foreach ($pin in $requiredPins) {
    if (-not $workflow.Contains($pin)) {
        Write-Error "FAIL workflow is missing reviewed pin $pin" -ErrorAction Continue
        $failures++
    }
}
foreach ($required in @(
        "runs-on: windows-2022", "VEU_APP_PRIVATE_KEY", "permission-contents: write",
        "4924807", "Iv23liCPYfquh6BtQ15Y",
        "VEU_RELEASE_IMMUTABILITY_ENABLED", "VEU clean-machine compatibility",
        "sha256sum --check SHA256SUMS.txt", "refs/heads/main",
        '$downloadHeaders.Accept="application/octet-stream"', '${upload}?name=',
        'while ($batch.Count -eq 100)', "equal or newer stable release",
        "cancel-in-progress: false", '!v*-dev.*', '!v*-ci.*')) {
    if (-not $workflow.Contains($required)) {
        Write-Error "FAIL workflow is missing release gate $required" -ErrorAction Continue
        $failures++
    }
}
# Invoke-RestMethod writes JSON arrays as one pipeline object: test the exact release-list loop.
$listing=[regex]::Match($workflow,'(?ms)^\s*\$all=@\(\); \$page=1\r?\n.*?^\s*\} while \(\$batch.Count -eq 100\)')
if (-not $listing.Success) { throw "Release pagination loop is unavailable." }
foreach($size in @(0,1,100,201)) {
    $result=& {
        param($size,$listing)
        $script:fixtureRequests=0
        function Invoke-RestMethod($Headers,$Uri) {
            $script:fixtureRequests++
            $pageNumber=[int]([regex]::Match($Uri,'[?&]page=([0-9]+)').Groups[1].Value)
            $records=@(for($index=($pageNumber-1)*100; $index -lt [Math]::Min($pageNumber*100,$size); $index++) {
                [pscustomobject]@{tag_name="v0.1.0-dev.$($index+1)+0123456"}
            })
            return ,$records
        }
        $h=@{}; $api="https://fixture.invalid"
        . ([scriptblock]::Create($listing.Value))
        [pscustomobject]@{Count=$all.Count;Requests=$script:fixtureRequests;Nested=@($all|Where-Object {$_ -is [array]}).Count}
    } $size $listing
    if ($result.Count -ne $size -or $result.Nested -ne 0 -or $result.Requests -ne ([Math]::Floor($size/100)+1)) {
        Write-Error "FAIL release pagination shape for $size records: $($result|ConvertTo-Json -Compress)" -ErrorAction Continue
        $failures++
    }
}
if ($workflow -match 'uses:\s+[^\r\n]+@(v|main|master)(?:[0-9.]*)\s') {
    Write-Error "FAIL workflow contains a mutable action reference" -ErrorAction Continue
    $failures++
}
if ($workflow.Contains("workflow_dispatch:")) {
    Write-Error "FAIL native publication must not expose a human manual-dispatch shortcut" -ErrorAction Continue
    $failures++
}
if ($failures) { throw "$failures pipeline validation test(s) failed." }
Write-Host "Pipeline validation tests passed."
# Clear the nonzero code from expected-failure child processes for the Actions pwsh wrapper.
exit 0
