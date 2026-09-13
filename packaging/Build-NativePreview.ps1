[CmdletBinding()]
param([string]$Version = '0.02-public')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $projectRoot 'engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo'
$packageRoot = Join-Path $projectRoot "dist/PulseWeaverStudio-v$Version"
$zipPath = "$packageRoot.zip"
if ((Test-Path -LiteralPath $packageRoot) -or (Test-Path -LiteralPath $zipPath)) { throw 'Package already exists. Choose a new version or review the previous output.' }
if (-not (Test-Path -LiteralPath "$runtimeRoot/bin/64bit/PulseWeaverCore.exe")) { throw 'Build the native application first.' }
New-Item -ItemType Directory -Path $packageRoot -Force | Out-Null
foreach ($folder in @('bin','data','obs-plugins')) { Copy-Item -LiteralPath (Join-Path $runtimeRoot $folder) -Destination $packageRoot -Recurse }
Get-ChildItem -LiteralPath $packageRoot -Recurse -File -Filter '*.pdb' | Remove-Item -Force
foreach ($doc in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','UPSTREAM.md')) { Copy-Item -LiteralPath (Join-Path $projectRoot $doc) -Destination $packageRoot }
New-Item -ItemType File -Path "$packageRoot/portable_mode.txt" | Out-Null
if (Test-Path -LiteralPath "$packageRoot/config") { throw 'Configuration must not be packaged.' }
Compress-Archive -Path "$packageRoot/*" -DestinationPath $zipPath -CompressionLevel Optimal
Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
