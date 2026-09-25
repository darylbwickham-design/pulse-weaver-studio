[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '1.13.0',
    [Parameter(Mandatory)][string]$YouTubeDesktopClientJson,
    [string]$RuntimeRoot
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'YouTubeDesktopRegistration.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($RuntimeRoot)) {
    $RuntimeRoot = Join-Path $projectRoot 'engine/obs-studio/build_motion_preview/rundir/RelWithDebInfo'
}
$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$artifactRoot = Join-Path $projectRoot "artifacts/motion-preview-$Version"
$payloadRoot = Join-Path $artifactRoot "PulseWeaverMotionPreview-v$Version"
$payloadArchive = "$payloadRoot.zip"
$publishRoot = Join-Path $artifactRoot 'installer-publish'
$setupName = "PulseWeaver-Motion-Preview-Setup-$Version.exe"
$setupPath = Join-Path $artifactRoot $setupName
$lumiaName = "PulseWeaver-Motion-Preview-Lumia-1.3.0.lumiaplugin"
$lumiaPath = Join-Path $artifactRoot $lumiaName
$frontend = Join-Path $RuntimeRoot 'bin/64bit/PulseWeaverCore.exe'
$core = Join-Path $RuntimeRoot 'obs-plugins/64bit/pulse-weaver-core.dll'

if (Test-Path -LiteralPath $artifactRoot) {
    throw "The preview artifact folder already exists: $artifactRoot"
}
if (-not (Test-Path -LiteralPath $frontend) -or -not (Test-Path -LiteralPath $core)) {
    throw 'Build the Motion Preview frontend and native core before packaging.'
}

$youtubeRegistration = Get-YouTubeDesktopRegistration -Path $YouTubeDesktopClientJson
New-Item -ItemType Directory -Path $payloadRoot | Out-Null
foreach ($folder in @('bin', 'data', 'obs-plugins')) {
    Copy-Item -LiteralPath (Join-Path $RuntimeRoot $folder) -Destination $payloadRoot -Recurse
}
$registrationDirectory = Join-Path $payloadRoot 'data/pulse-weaver'
New-Item -ItemType Directory -Path $registrationDirectory -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $registrationDirectory 'youtube-desktop-client.json'), $youtubeRegistration)
New-Item -ItemType File -Path (Join-Path $payloadRoot 'portable_mode.txt') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README-MOTION-PREVIEW.txt') -Destination (Join-Path $payloadRoot 'README-FIRST.txt')
New-Item -ItemType Directory -Path (Join-Path $payloadRoot 'docs') | Out-Null
foreach ($doc in @('PRIVACY.md', 'TERMS.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $doc) -Destination (Join-Path $payloadRoot 'docs' $doc)
}
Get-ChildItem -LiteralPath $payloadRoot -Recurse -File -Filter '*.pdb' | Remove-Item -Force

$forbidden = Get-ChildItem -LiteralPath $payloadRoot -Recurse -File | Where-Object {
    $_.Extension -in @('.pdb', '.log') -or $_.Name -ieq 'app-credentials.ini'
}
if ($forbidden) {
    throw 'The preview payload contains a debug or private-state file.'
}
if (Test-Path -LiteralPath (Join-Path $payloadRoot 'config')) {
    throw 'Configuration must not be packaged.'
}
Compress-Archive -Path (Join-Path $payloadRoot '*') -DestinationPath $payloadArchive -CompressionLevel Optimal

& (Join-Path $PSScriptRoot 'Build-LumiaMotionPreview.ps1') -OutputDirectory $artifactRoot | Out-Null

& dotnet publish (Join-Path $projectRoot 'packaging/PulseWeaver.PrivateSetup/PulseWeaver.PrivateSetup.csproj') `
    -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:PublishTrimmed=false `
    "-p:ReleaseVersion=$Version" '-p:InstallerSuffix=MOTION-PREVIEW' '-p:PreviewChannel=true' `
    "-p:PayloadArchive=$payloadArchive" -o $publishRoot
if ($LASTEXITCODE -ne 0) { throw "Installer build failed with exit code $LASTEXITCODE." }
$publishedSetup = Get-ChildItem -LiteralPath $publishRoot -File -Filter '*.exe' | Where-Object Name -Like 'PulseWeaver-Setup-*' | Select-Object -First 1
if (-not $publishedSetup) { throw 'The installer executable was not produced.' }
Copy-Item -LiteralPath $publishedSetup.FullName -Destination $setupPath

$hashLines = foreach ($file in @($setupPath, $payloadArchive, $lumiaPath)) {
    $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
    "$($hash.Hash)  $([IO.Path]::GetFileName($file))"
}
[IO.File]::WriteAllLines((Join-Path $artifactRoot 'SHA256SUMS.txt'), $hashLines)

[pscustomobject]@{
    Installer = $setupPath
    LumiaPlugin = $lumiaPath
    Payload = $payloadArchive
    InstallLocation = Join-Path $env:LOCALAPPDATA 'Programs/Pulse Weaver Motion Preview'
    Port = 18765
}
