# Builds the co-op test kit: dist\cdmp-coop-<date>.zip with the ASI loader,
# the mod, the host's and the guest's configs, and the instructions.
#
#   .\scripts\package_coop.ps1            # uses the RelWithDebInfo build

param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Config = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root "build\bin\$Config"
foreach ($f in 'cdmp.asi') {
    if (-not (Test-Path (Join-Path $bin $f))) { throw "Build first: $f not found in $bin" }
}

$stamp = Get-Date -Format 'yyyyMMdd'
$staging = Join-Path $root "dist\cdmp-coop-$stamp"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force $staging | Out-Null

$loader = Join-Path $root 'third_party\ultimate-asi-loader\v9.7.4\dinput8.dll'
if (-not (Test-Path $loader)) { throw 'Ultimate ASI Loader not found: see third_party\ultimate-asi-loader\README.md' }
Copy-Item $loader (Join-Path $staging 'winmm.dll')
Copy-Item (Join-Path $bin 'cdmp.asi') $staging
# Two profiles; cdmp.ini starts as the guest's (the host renames theirs).
Copy-Item (Join-Path $root 'kit\coop\cdmp.ini.host') $staging
Copy-Item (Join-Path $root 'kit\coop\cdmp.ini.guest') $staging
Copy-Item (Join-Path $root 'kit\coop\cdmp.ini.guest') (Join-Path $staging 'cdmp.ini')
Copy-Item (Join-Path $root 'kit\coop\README.txt') $staging
# Keeping the progress (README, section 6): both run with the game closed.
Copy-Item (Join-Path $root 'tools\scripts\apply-guest-progress.ps1') $staging
Copy-Item (Join-Path $root 'scripts\host_save_restore.ps1') $staging

Get-ChildItem $staging -File | Where-Object Name -ne 'SHA256.txt' | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower(), $_.Name
} | Set-Content -Encoding ascii (Join-Path $staging 'SHA256.txt')

$zip = "$staging.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip
Write-Host "Co-op kit: $zip ($([math]::Round((Get-Item $zip).Length / 1KB)) KB)"
