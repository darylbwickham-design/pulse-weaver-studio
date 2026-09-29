$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../packaging/RuntimePayloadPolicy.ps1')
$root = Join-Path ([IO.Path]::GetTempPath()) ('PulseWeaver-payload-policy-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$checks = 0
try {
    foreach ($private in @('app-credentials.ini','global.ini','basic.ini','service.json','youtube-streams.ini','debug.log',
        'config/profile.txt','plugin_config/state.txt','browser_profile/cookies')) {
        $path = Join-Path $root $private
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
        [IO.File]::WriteAllText($path,'synthetic')
        $rejected = $false
        try { Assert-PulseWeaverRuntimeTree -Root $root } catch { $rejected = $true }
        if (-not $rejected) { throw "Private payload accepted: $private" }
        $checks++
        Remove-Item -LiteralPath $path
        $parent = [IO.Path]::GetDirectoryName($path)
        if ($parent -ne $root) { Remove-Item -LiteralPath $parent }
    }
    $rejected = $false
    try { Assert-PulseWeaverRuntimeComponents -Root $root -IncludeChat } catch { $rejected = $true }
    if (-not $rejected) { throw 'Empty runtime accepted.' }
    $checks++
    $existing = Join-Path $PSScriptRoot '../../artifacts/channel-v1.14.0-unstable.10/payload'
    if (Test-Path -LiteralPath $existing) {
        Assert-PulseWeaverRuntimeComponents -Root $existing -IncludeChat
        Assert-PulseWeaverRuntimeTree -Root $existing
        $checks += 2
    }
    Write-Output "PASS: $checks packaging checks; private state rejected, required binaries checked, existing clean payload accepted."
} finally {
    $full = [IO.Path]::GetFullPath($root)
    $temp = [IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetTempPath()) + [IO.Path]::DirectorySeparatorChar
    if ($full.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($full).StartsWith('PulseWeaver-payload-policy-',[StringComparison]::Ordinal)) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}
