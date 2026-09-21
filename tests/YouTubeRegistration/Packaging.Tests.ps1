$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../../packaging/YouTubeDesktopRegistration.ps1')
$directory = Join-Path ([IO.Path]::GetTempPath()) ('PulseWeaver-registration-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $directory | Out-Null
try {
    $file = Join-Path $directory 'client.json'
    [IO.File]::WriteAllText($file, '{"installed":{"client_id":"fixture.apps.googleusercontent.com","client_secret":"fixture-secret","refresh_token":"not-for-distribution","project_id":"unused"},"access_token":"never"}')
    $result = Get-YouTubeDesktopRegistration -Path $file
    $parsed = $result | ConvertFrom-Json
    if ($parsed.installed.client_id -ne 'fixture.apps.googleusercontent.com' -or
        $parsed.installed.client_secret -ne 'fixture-secret' -or $result -match 'refresh_token|access_token|project_id|never|not-for-distribution') {
        throw 'Registration allowlist failed.'
    }
    foreach ($invalid in @('{}','{"web":{"client_id":"fixture.apps.googleusercontent.com","client_secret":"secret"}}',
        '{"installed":{"client_id":"fixture.apps.googleusercontent.com","client_secret":""}}','not JSON')) {
        [IO.File]::WriteAllText($file, $invalid)
        $rejected = $false
        try { $null = Get-YouTubeDesktopRegistration -Path $file } catch { $rejected = $true }
        if (-not $rejected) { throw 'Invalid registration accepted.' }
    }
    Write-Output 'PASS: packaging includes only desktop app registration; rejects web/missing/malformed credentials.'
} finally {
    # Explicit disposable directory created above; never a profile or workspace.
    if ((Split-Path $directory -Leaf) -like 'PulseWeaver-registration-test-*' -and
        (Split-Path $directory -Parent) -eq [IO.Path]::TrimEndingDirectorySeparator([IO.Path]::GetTempPath())) {
        Remove-Item -LiteralPath $directory -Recurse -Force
    }
}
