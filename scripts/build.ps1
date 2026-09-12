param([ValidateSet("Debug","Release")][string]$Configuration = "Debug", [switch]$NoTests)
$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$locator = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = & $locator -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw "Install Visual Studio 2022 Build Tools with Desktop development with C++." }
$cmakeExe = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$buildDir = Join-Path $projectRoot "out\native"
& $cmakeExe -S $projectRoot -B $buildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE) { throw "CMake configure failed." }
& $cmakeExe --build $buildDir --config $Configuration --parallel 4
if ($LASTEXITCODE) { throw "Build failed." }
if (-not $NoTests) {
    $ctestExe = Join-Path (Split-Path $cmakeExe -Parent) "ctest.exe"
    & $ctestExe --test-dir $buildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE) { throw "Tests failed." }
}
Write-Host "Built: $buildDir\$Configuration\VelocityNetTools.exe"
