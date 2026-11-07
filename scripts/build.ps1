# Configures and builds the project with the installed Visual Studio Build
# Tools, then runs the unit tests.
#
#   .\scripts\build.ps1                    # RelWithDebInfo + tests
#   .\scripts\build.ps1 -Config Debug
#   .\scripts\build.ps1 -SkipTests

param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'RelWithDebInfo',
    [switch]$SkipTests
)
# Native tools write warnings to stderr; with 'Stop' PowerShell 5.1 would turn
# those into terminating errors. Failures are detected via $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $root 'build'

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$cache = Join-Path $buildDir 'CMakeCache.txt'
$generator = $null
$vs = $null
if (Test-Path -LiteralPath $cache) {
    # A configured tree is tied to both its generator and VS instance. In particular,
    # CMake can use a newer VS installation to service an older VS generator, so do
    # not replace either choice just because vswhere reports a newer installation.
    foreach ($line in Get-Content -LiteralPath $cache) {
        if ($line -match '^CMAKE_GENERATOR:INTERNAL=(.+)$') { $generator = $Matches[1] }
        if ($line -match '^CMAKE_GENERATOR_INSTANCE:INTERNAL=(.+)$') { $vs = $Matches[1] }
    }
    if (-not $vs -or -not (Test-Path -LiteralPath $vs)) {
        throw "The configured Visual Studio instance no longer exists: $vs. Remove '$buildDir' and configure again."
    }
} else {
    $vsJson = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json
    $instance = $vsJson | ConvertFrom-Json | Select-Object -First 1
    if (-not $instance) { throw 'Visual Studio / Build Tools with the C++ x64 toolset not found' }
    $vs = $instance.installationPath
    $major = [int]($instance.installationVersion.Split('.')[0])
    $generator = switch ($major) {
        18 { 'Visual Studio 18 2026' }
        17 { 'Visual Studio 17 2022' }
        default { throw "Unsupported Visual Studio major version $major at $vs" }
    }
}
$cmakeBin = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmake = Join-Path $cmakeBin 'cmake.exe'
$ctest = Join-Path $cmakeBin 'ctest.exe'
if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ctest)) {
    throw "The CMake tools were not found in the selected Visual Studio instance: $vs"
}

$configure = @('-S', $root, '-B', $buildDir, '-Wno-dev', '-Wno-deprecated')
if (-not (Test-Path -LiteralPath $cache)) { $configure += @('-G', $generator, '-A', 'x64') }
& $cmake @configure
if ($LASTEXITCODE) { throw "CMake configure failed ($LASTEXITCODE)" }

& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)" }

if (-not $SkipTests) {
    # The script tests run in Windows PowerShell 5.1. Started from PowerShell 7 they would inherit its PSModulePath, and
    # 5.1 then cannot load its own modules (Get-FileHash "not recognized"): give them none, so 5.1 uses its defaults.
    $savedModulePath = $env:PSModulePath
    if ($PSVersionTable.PSEdition -eq 'Core') { Remove-Item Env:PSModulePath -ErrorAction SilentlyContinue }
    try {
        & $ctest --test-dir $buildDir -C $Config --output-on-failure
    } finally {
        $env:PSModulePath = $savedModulePath
    }
    if ($LASTEXITCODE) { throw "Tests failed ($LASTEXITCODE)" }
}

Write-Host "Build OK: $root\build\bin\$Config"
