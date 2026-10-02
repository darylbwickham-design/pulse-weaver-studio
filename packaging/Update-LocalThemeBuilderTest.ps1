[CmdletBinding()]
param([string]$DefaultsFile)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$testPath = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs/Pulse Weaver 1.14 Clean Test'))
$runtimePath = Join-Path $repoPath 'engine/obs-studio/build_unstable/rundir/RelWithDebInfo'
if (-not (Test-Path -LiteralPath (Join-Path $testPath 'portable_mode.txt'))) { throw 'The isolated portable installation is missing.' }
if (Get-Process PulseWeaverCore -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($testPath + '\', [StringComparison]::OrdinalIgnoreCase) }) { throw 'Close the isolated test window before updating.' }
$backupPath = Join-Path $repoPath ('artifacts/builder-1142/theme-backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
$files = @('obs-plugins/64bit/pulse-weaver-core.dll', 'bin/64bit/PulseWeaverCore.exe')
foreach ($relative in $files) {
    $target = [IO.Path]::GetFullPath((Join-Path $testPath $relative))
    if (-not $target.StartsWith($testPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid isolated target path.' }
    $snapshot = Join-Path $backupPath $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $snapshot) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $snapshot
    Copy-Item -LiteralPath (Join-Path $runtimePath $relative) -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath (Join-Path $runtimePath $relative)).Hash) { throw 'Runtime copy did not verify.' }
}
$configPath = Join-Path $testPath 'config/obs-studio/plugin_config/pulse-weaver-core'
$defaultsPath = Join-Path $configPath 'show-builder-defaults.json'
if (Test-Path -LiteralPath $defaultsPath) { Copy-Item -LiteralPath $defaultsPath -Destination (Join-Path $backupPath 'show-builder-defaults.json') }
if ($DefaultsFile) {
    $defaultsText = Get-Content -LiteralPath $DefaultsFile -Raw
    $defaults = $defaultsText | ConvertFrom-Json
    if (-not $defaults.overlays) { throw 'The defaults file must contain an overlays object.' }
    [IO.File]::WriteAllText($defaultsPath, $defaultsText)
}
@'
Pulse Weaver 1.14.2 — theme builder candidate
Open Show Control -> Control -> Build my show.
Choose Stage themes, select Looks, assign sources, review overlays and framing.
Three overlay slots per canvas support local provider URLs and existing sources.
Provider URLs are kept in the portable profile, not public templates.
Camera-above/activity-below portrait layouts and a dimmed left clipped-corner
Starting camera are available. Browser overlays are placeholders during review.
This installation retains its existing portable profile and API port 18775.
'@ | Set-Content -LiteralPath (Join-Path $testPath 'BUILDER-TEST.txt')
[pscustomobject]@{ Installation = $testPath; Backup = $backupPath; Defaults = $defaultsPath }
