[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '1.3.1',
    [string]$OutputDirectory,
    [string]$ConfigPath
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $projectRoot 'artifacts/lumia-motion-preview' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$stage = Join-Path $OutputDirectory "package-$Version"
$packagePath = Join-Path $OutputDirectory "PulseWeaver-Motion-Preview-Lumia-$Version.lumiaplugin"
if (Test-Path -LiteralPath $packagePath) { throw "Package already exists: $packagePath" }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach ($entry in @('main.js', 'manifest.json', 'package.json', 'assets')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "integrations/lumia-pulseweaver/$entry") -Destination $stage -Recurse
}
$manifestPath = Join-Path $stage 'manifest.json'
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
$manifest.id = 'pulseweavermotionpreview'
$manifest.name = 'Pulse Weaver Motion Preview'
$manifest.version = $Version
$manifest.description = 'Control the isolated Pulse Weaver Motion Preview: stages, animated looks, original-scene restore, sources and audio. Coexists with the release plugin.'
foreach ($setting in $manifest.config.settings) {
    if ($setting.key -eq 'port') {
        $setting.defaultValue = 18765
        $setting.helperText = 'Motion Preview uses port 18765. This plugin is separate from the regular Pulse Weaver plugin.'
    }
    if ($setting.key -eq 'configPath' -and $ConfigPath) {
        $setting | Add-Member -NotePropertyName defaultValue -NotePropertyValue ([IO.Path]::GetFullPath($ConfigPath)) -Force
    }
}
$manifest | ConvertTo-Json -Depth 60 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$npmPath = Join-Path $stage 'package.json'
$npm = Get-Content -Raw -LiteralPath $npmPath | ConvertFrom-Json
$npm.name = 'pulseweaver-motion-preview-lumia-plugin'
$npm.version = $Version
$npm | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $npmPath -Encoding utf8
@'
# Pulse Weaver Motion Preview for Lumia

This package has its own manifest ID, `pulseweavermotionpreview`. Import it alongside the regular Pulse Weaver plugin; existing release bindings remain separate.

The default port is 18765. For a portable task build, set Custom Pulse Weaver configuration path to that build's config/obs-studio/plugin_config/pulse-weaver-core/pulse-weaver.ini. No token is bundled.

Choose **Run Motion Action** to select a named stage look. Pulse Weaver changes stage through its configured stinger when needed, then runs the saved look. Looks within the same stage use source motion. **Stop and Restore Motion**, **Restore Last Motion** and **Restore Original Scenes** provide recovery controls.

Variables and action results belong to the `pulseweavermotionpreview` namespace. Do not reuse release-plugin variable names in preview-only action chains.
'@ | Set-Content -LiteralPath (Join-Path $stage 'README.md') -Encoding utf8
$zipPath = [IO.Path]::ChangeExtension($packagePath, '.zip')
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal
Move-Item -LiteralPath $zipPath -Destination $packagePath
$hash = Get-FileHash -LiteralPath $packagePath -Algorithm SHA256
"$($hash.Hash)  $([IO.Path]::GetFileName($packagePath))" | Set-Content -LiteralPath (Join-Path $OutputDirectory 'SHA256SUMS.txt')
Write-Output $packagePath
