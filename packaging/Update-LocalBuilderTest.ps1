[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$testPath = Join-Path $env:LOCALAPPDATA 'Programs\Pulse Weaver 1.14 Clean Test'
$referencePath = Join-Path $env:LOCALAPPDATA 'Programs\Pulse Weaver\config\obs-studio'
$payloadPath = Join-Path $repoPath 'artifacts\channel-v1.14.2-alpha.1\payload'
$corePath = Join-Path $repoPath 'engine\obs-studio\build_unstable\rundir\RelWithDebInfo\obs-plugins\64bit\pulse-weaver-core.dll'
$backupPath = Join-Path $repoPath ('artifacts\builder-1142\backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$testPath = [IO.Path]::GetFullPath($testPath)
$expectedPath = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'Programs\Pulse Weaver 1.14 Clean Test'))
if ($testPath -ne $expectedPath -or -not (Test-Path -LiteralPath (Join-Path $testPath 'portable_mode.txt'))) {
    throw 'The isolated portable installation could not be verified.'
}
$testProcess = Get-Process -Name PulseWeaverCore -ErrorAction SilentlyContinue | Where-Object {
    $_.Path -and [IO.Path]::GetFullPath($_.Path).StartsWith($testPath + '\', [StringComparison]::OrdinalIgnoreCase)
}
if ($testProcess) { throw 'Close the isolated builder test before updating its files.' }
foreach ($required in @($payloadPath, $corePath, (Join-Path $referencePath 'basic\scenes\og_scenes-pulse-import.json'),
    (Join-Path $referencePath 'pulseweaver-stages.json'),
    (Join-Path $referencePath 'plugin_config\pulse-weaver-core\pulseweaver-motion-actions.json'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing required build or reference file: $required" }
}
New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
Copy-Item -LiteralPath $testPath -Destination (Join-Path $backupPath 'installation') -Recurse
$referenceFiles = @('basic\scenes\og_scenes-pulse-import.json', 'pulseweaver-stages.json',
    'plugin_config\pulse-weaver-core\pulseweaver-motion-actions.json')
$referenceHashes = @{}
foreach ($relative in $referenceFiles) {
    $source = Join-Path $referencePath $relative
    $referenceHashes[$relative] = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    $snapshot = Join-Path $backupPath ('reference\' + $relative)
    New-Item -ItemType Directory -Path (Split-Path -Parent $snapshot) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $snapshot
}
foreach ($folder in @('bin', 'data', 'obs-plugins', 'docs')) {
    New-Item -ItemType Directory -Path (Join-Path $testPath $folder) -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $payloadPath $folder) -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $testPath $folder) -Recurse -Force
    }
}
Copy-Item -LiteralPath $corePath -Destination (Join-Path $testPath 'obs-plugins\64bit\pulse-weaver-core.dll') -Force
$configPath = Join-Path $testPath 'config\obs-studio'
$referenceCollection = Get-Content -LiteralPath (Join-Path $backupPath 'reference\basic\scenes\og_scenes-pulse-import.json') -Raw | ConvertFrom-Json
$oldCollectionName = $referenceCollection.name
$referenceCollection.name = 'Builder Reference'
$referenceCollection.current_scene = 'PW Starting'
$referenceCollection.current_program_scene = 'PW Starting'
[IO.File]::WriteAllText((Join-Path $configPath 'basic\scenes\builder-reference.json'), ($referenceCollection | ConvertTo-Json -Depth 100))
$motion = Get-Content -LiteralPath (Join-Path $backupPath 'reference\plugin_config\pulse-weaver-core\pulseweaver-motion-actions.json') -Raw | ConvertFrom-Json
foreach ($look in $motion.actions) {
    if ($look.collection -eq $oldCollectionName) { $look.collection = 'Builder Reference' }
}
[IO.File]::WriteAllText((Join-Path $configPath 'plugin_config\pulse-weaver-core\pulseweaver-motion-actions.json'), ($motion | ConvertTo-Json -Depth 100))
Copy-Item -LiteralPath (Join-Path $backupPath 'reference\pulseweaver-stages.json') -Destination (Join-Path $configPath 'pulseweaver-stages.json') -Force
$userPath = Join-Path $configPath 'user.ini'
$userSettings = Get-Content -LiteralPath $userPath -Raw
$userSettings = $userSettings -replace '(?m)^SceneCollection=.*$', 'SceneCollection=Builder Reference'
$userSettings = $userSettings -replace '(?m)^SceneCollectionFile=.*$', 'SceneCollectionFile=builder-reference.json'
[IO.File]::WriteAllText($userPath, $userSettings)
$settingsPath = Join-Path $configPath 'plugin_config\pulse-weaver-core\pulse-weaver.ini'
$settings = Get-Content -LiteralPath $settingsPath -Raw
$settings = $settings -replace '(?m)^port=\d+[ \t]*\r?$', 'port=18775'
[IO.File]::WriteAllText($settingsPath, $settings)
$globalPath = Join-Path $configPath 'global.ini'
$globalSettings = (Get-Content -LiteralPath $globalPath -Raw) -replace '(?m)^EnableAutoUpdates=.*$', 'EnableAutoUpdates=false'
[IO.File]::WriteAllText($globalPath, $globalSettings)
foreach ($relative in $referenceFiles) {
    if ((Get-FileHash -LiteralPath (Join-Path $referencePath $relative) -Algorithm SHA256).Hash -ne $referenceHashes[$relative]) {
        throw 'The reference changed during snapshot preparation. Recheck the snapshot before using this test.'
    }
}
if ((Get-FileHash -LiteralPath (Join-Path $testPath 'obs-plugins\64bit\pulse-weaver-core.dll') -Algorithm SHA256).Hash -ne
    (Get-FileHash -LiteralPath $corePath -Algorithm SHA256).Hash) { throw 'The new builder DLL copy did not verify.' }
$instructions = @'
Pulse Weaver 1.14.2 — local animated builder test, pass 1

Open Show Control → Control → Build my show.
Use existing Stage compositions is selected. PW stages are the main-instance
reference snapshot. Name your new show Builder Test, review and finish.
The generated show has its own scenes, looks and paired portrait framing.
Edit and save its looks in Control, then run a saved look to rehearse output.
The main installation is the physical reference; this installation uses its own
portable profile and API port 18775. Account credentials were not copied.

The old clean-test collection remains available as Untitled. Its complete
installation and the exact reference snapshot are backed up under the checkout's
artifacts/builder-1142 directory. This candidate is local and is not a release.
'@
[IO.File]::WriteAllText((Join-Path $testPath 'BUILDER-TEST.txt'), $instructions)
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Pulse Weaver 1.14.2 Builder Test.lnk'))
$shortcut.TargetPath = Join-Path $testPath 'bin\64bit\PulseWeaverCore.exe'
$shortcut.WorkingDirectory = Join-Path $testPath 'bin\64bit'
$shortcut.Arguments = '--portable --multi'
$shortcut.Description = 'Local Pulse Weaver 1.14.2 animated scene builder test — API port 18775'
$shortcut.Save()
[pscustomobject]@{ Installation=$testPath; Backup=$backupPath; Version='1.14.2'; Reference='Builder Reference'; Port=18775 }
