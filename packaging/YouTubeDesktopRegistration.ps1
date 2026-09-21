function Get-YouTubeDesktopRegistration {
    param([Parameter(Mandatory)][string]$Path)
    $document = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    if ($null -ne $document.web -or $null -eq $document.installed) {
        throw 'YouTube requires a Google Desktop app (installed) client JSON, never a web-app secret.'
    }
    $client = $document.installed
    if ($client.client_id -isnot [string] -or $client.client_secret -isnot [string] -or
        $client.client_id -notmatch '^[A-Za-z0-9_-]+\.apps\.googleusercontent\.com$' -or
        [string]::IsNullOrWhiteSpace($client.client_secret)) {
        throw 'The Google Desktop app registration is incomplete.'
    }
    # Explicit allowlist: never copy account tokens, personal configuration, or
    # unrelated OAuth clients from the input into the redistributable payload.
    return ([ordered]@{installed=[ordered]@{
        client_id=$client.client_id.Trim(); client_secret=$client.client_secret.Trim()
    }} | ConvertTo-Json -Compress)
}
