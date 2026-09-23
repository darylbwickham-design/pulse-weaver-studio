[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '1.12.15',
    [Parameter(Mandatory)][string]$YouTubeDesktopClientJson
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'YouTubeDesktopRegistration.ps1')
$youtubeRegistration = Get-YouTubeDesktopRegistration -Path $YouTubeDesktopClientJson
$projectRoot = Split-Path -Parent $PSScriptRoot
$runtimeRoot = Join-Path $projectRoot 'engine/obs-studio/build_pw_vs1714_sdk22621/rundir/RelWithDebInfo'
$destination = Join-Path $projectRoot "dist/PulseWeaverStudio-v$Version-beta"
$archive = "$destination.zip"
$frontend = Join-Path $runtimeRoot 'bin/64bit/PulseWeaverCore.exe'
$core = Join-Path $runtimeRoot 'obs-plugins/64bit/pulse-weaver-core.dll'
$cachePath = Join-Path $projectRoot 'engine/obs-studio/build_pw_vs1714_sdk22621/CMakeCache.txt'

if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $archive)) {
    throw "v$Version package already exists."
}
if (-not (Test-Path -LiteralPath $frontend) -or -not (Test-Path -LiteralPath $core)) {
    throw 'Build the complete native frontend and Pulse Weaver core before packaging v126.'
}
if (-not (Test-Path -LiteralPath $cachePath)) {
    throw 'The native build cache is missing.'
}

$cache = Get-Content -LiteralPath $cachePath -Raw
foreach ($key in @('YOUTUBE_CLIENTID', 'YOUTUBE_SECRET')) {
    if ($cache -notmatch "(?m)^$key`:[^=]*=\s*$") {
        throw "$key must be empty. Desktop registration is supplied separately during packaging."
    }
}

$legacyFrontendHash = '1985A04612B5DE1296426C6F4FF4AFBDDF28115EA36BD2B5B2D567956B8AF782'
$legacyCoreHash = 'CE763C5F4BA82F7B695B9A4931FDC3307C94079ECBC9F3AF61470BA4E9A7D565'
if ((Get-FileHash -LiteralPath $frontend -Algorithm SHA256).Hash -eq $legacyFrontendHash -or
    (Get-FileHash -LiteralPath $core -Algorithm SHA256).Hash -eq $legacyCoreHash) {
    throw 'The native build still contains a legacy private frontend or core. Rebuild both targets first.'
}

New-Item -ItemType Directory -Path $destination | Out-Null
foreach ($folder in @('bin', 'data', 'obs-plugins')) {
    Copy-Item -LiteralPath (Join-Path $runtimeRoot $folder) -Destination $destination -Recurse
}
$registrationDirectory = Join-Path $destination 'data/pulse-weaver'
New-Item -ItemType Directory -Path $registrationDirectory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $registrationDirectory 'youtube-desktop-client.json'), $youtubeRegistration)
Get-ChildItem -LiteralPath $destination -Recurse -File -Filter '*.pdb' | Remove-Item -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot "README-FIRST-$Version-private.txt") -Destination (Join-Path $destination 'README-FIRST.txt')
New-Item -ItemType Directory -Path (Join-Path $destination 'docs') | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'PRIVACY.md') -Destination (Join-Path $destination 'docs/PRIVACY.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'TERMS.md') -Destination (Join-Path $destination 'docs/TERMS.md')
New-Item -ItemType File -Path (Join-Path $destination 'portable_mode.txt') | Out-Null

if (Test-Path -LiteralPath (Join-Path $destination 'config')) {
    throw 'The distribution must not contain personal configuration.'
}
$forbidden = Get-ChildItem -LiteralPath $destination -Recurse -File | Where-Object {
    $_.Extension -in @('.pdb', '.log') -or $_.Name -ieq 'app-credentials.ini'
}
if ($forbidden) {
    throw 'The distribution contains a forbidden private-state or debug file.'
}

Compress-Archive -Path (Join-Path $destination '*') -DestinationPath $archive -CompressionLevel Optimal
Get-FileHash -LiteralPath $archive -Algorithm SHA256
