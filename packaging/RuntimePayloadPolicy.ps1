function Assert-PulseWeaverRuntimeTree {
    param([Parameter(Mandatory)][string]$Root)
    $resolved = (Resolve-Path -LiteralPath $Root -ErrorAction Stop).Path
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($resolved)
    while ($pending.Count -gt 0) {
        $path = $pending.Pop()
        $item = Get-Item -LiteralPath $path -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw 'Linked runtime files or directories must not be included in a distributable payload.'
        }
        if ($item.PSIsContainer) {
            if ($item.Name -in @('config','config-backups','logs','crashes','plugin_config','browser_profile')) {
                throw 'Private configuration directory was found in the runtime payload.'
            }
            foreach ($child in Get-ChildItem -LiteralPath $path -Force -ErrorAction Stop) { $pending.Push($child.FullName) }
        } elseif ($item.Name -in @('app-credentials.ini','pulse-weaver.ini','pulseweaver-motion-actions.json',
            'youtube-streams.ini','global.ini','basic.ini','service.json') -or $item.Extension -eq '.log') {
            throw 'Private state was found in the runtime payload.'
        }
    }
}

function Assert-PulseWeaverRuntimeComponents {
    param([Parameter(Mandatory)][string]$Root, [switch]$IncludeChat)
    $required = @('bin/64bit/PulseWeaverCore.exe','bin/64bit/obs.dll','bin/64bit/obs-frontend-api.dll',
        'bin/64bit/Qt6Core.dll','bin/64bit/Qt6Gui.dll','bin/64bit/Qt6Widgets.dll','bin/64bit/Qt6Network.dll',
        'bin/64bit/platforms/qwindows.dll','bin/64bit/tls/qschannelbackend.dll',
        'obs-plugins/64bit/pulse-weaver-core.dll','data/obs-studio/locale.ini')
    if ($IncludeChat) { $required += 'bin/64bit/youtube-chat/PulseWeaver.YouTubeChat.exe' }
    foreach ($relative in $required) {
        $file = Get-Item -LiteralPath (Join-Path $Root $relative) -ErrorAction SilentlyContinue
        if ($null -eq $file -or $file.PSIsContainer -or $file.Length -le 0) { throw "Missing runtime component: $relative" }
    }
}
