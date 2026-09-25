[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('release','alpha')][string]$Channel,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [ValidateRange(1,999999)][int]$AlphaRevision = 1,
    [Parameter(Mandatory)][string]$RuntimeRoot,
    [Parameter(Mandatory)][string]$YouTubeDesktopClientJson
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'YouTubeDesktopRegistration.ps1')
$projectRoot = Split-Path -Parent $PSScriptRoot
$runtime = (Resolve-Path -LiteralPath $RuntimeRoot).Path
$tag = if ($Channel -eq 'alpha') { "v$Version-alpha.$AlphaRevision" } else { "v$Version" }
$output = Join-Path $projectRoot "artifacts/channel-$tag"
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }
$registration = Get-YouTubeDesktopRegistration -Path $YouTubeDesktopClientJson
foreach ($file in @('bin/64bit/PulseWeaverCore.exe','obs-plugins/64bit/pulse-weaver-core.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $file))) { throw "Missing runtime component: $file" }
}
$payload = Join-Path $output 'payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($folder in @('bin','data','obs-plugins')) {
    Copy-Item -LiteralPath (Join-Path $runtime $folder) -Destination $payload -Recurse
}
# Only the staged payload is stripped. The running build and its configuration are untouched.
$payloadPrefix = [IO.Path]::GetFullPath($payload) + [IO.Path]::DirectorySeparatorChar
Get-ChildItem -LiteralPath $payload -Recurse -File -Filter '*.pdb' | ForEach-Object {
    if (-not $_.FullName.StartsWith($payloadPrefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid staged path' }
    Remove-Item -LiteralPath $_.FullName
}
$privateFiles = Get-ChildItem -LiteralPath $payload -Recurse -File | Where-Object {
    $_.Name -in @('app-credentials.ini','pulse-weaver.ini','pulseweaver-motion-actions.json') -or $_.Extension -eq '.log'
}
if ($privateFiles) { throw 'Private state was found in the runtime payload.' }
$regDir = Join-Path $payload 'data/pulse-weaver'
New-Item -ItemType Directory -Path $regDir -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $regDir 'youtube-desktop-client.json'),$registration)
$identity = @{schema=1;channel=$(if($Channel -eq 'alpha'){'windows-alpha'}else{'windows-private'});tag=$tag}
$identity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $payload 'bin/64bit/pulseweaver-update.json') -Encoding utf8
New-Item -ItemType File -Path (Join-Path $payload 'portable_mode.txt') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $payload 'docs') | Out-Null
foreach ($doc in @('PRIVACY.md','TERMS.md')) { Copy-Item -LiteralPath (Join-Path $projectRoot $doc) -Destination (Join-Path $payload 'docs') }
Copy-Item -LiteralPath (Join-Path $projectRoot 'docs/ALPHA-UPGRADES.md') -Destination (Join-Path $payload 'docs')
$archive = Join-Path $output 'payload.zip'
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $archive -CompressionLevel Optimal
$suffix = if ($Channel -eq 'alpha') {'ALPHA'} else {'BETA'}
$publish = Join-Path $output 'setup'
& dotnet publish (Join-Path $PSScriptRoot 'PulseWeaver.PrivateSetup/PulseWeaver.PrivateSetup.csproj') -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true "-p:ReleaseVersion=$Version" "-p:InstallerSuffix=$suffix" "-p:AlphaChannel=$($Channel -eq 'alpha')" `
    "-p:AlphaRevision=$AlphaRevision" "-p:PayloadArchive=$archive" -o $publish
if ($LASTEXITCODE -ne 0) { throw 'Installer publish failed.' }
$name = if ($Channel -eq 'alpha') { "PulseWeaver-Setup-$Version-alpha.$AlphaRevision.exe" } else { "PulseWeaver-Setup-$Version-BETA.exe" }
$installer = Join-Path $output $name
Copy-Item -LiteralPath (Join-Path $publish "PulseWeaver-Setup-$Version-$suffix.exe") -Destination $installer
$pluginStage = Join-Path $output 'lumia'
New-Item -ItemType Directory -Path $pluginStage | Out-Null
foreach($entry in @('main.js','manifest.json','package.json','README.md','assets')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "integrations/lumia-pulseweaver/$entry") -Destination $pluginStage -Recurse
}
$pluginVersion = (Get-Content -LiteralPath (Join-Path $pluginStage 'manifest.json') -Raw | ConvertFrom-Json).version
$pluginZip = Join-Path $output "PulseWeaver-Lumia-$pluginVersion.zip"
Compress-Archive -Path (Join-Path $pluginStage '*') -DestinationPath $pluginZip
$plugin = [IO.Path]::ChangeExtension($pluginZip,'.lumiaplugin')
Move-Item -LiteralPath $pluginZip -Destination $plugin
@($installer,$plugin) | ForEach-Object { $h=Get-FileHash -LiteralPath $_ -Algorithm SHA256; "$($h.Hash)  $([IO.Path]::GetFileName($_))" } |
    Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt')
Write-Output $installer
