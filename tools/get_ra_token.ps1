# Fetches your RetroAchievements connect token from the ra_username and
# ra_password of a session_override.json, writes it as "ra_token" and removes
# the password from the file. Only retroachievements.org is contacted and
# nothing is printed but the result.
#   powershell -ExecutionPolicy Bypass -File tools\get_ra_token.ps1 -Json path\to\session_override.json
param([Parameter(Mandatory = $true)][string]$Json)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$cfg = Get-Content -LiteralPath $Json -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $cfg.ra_username -or -not $cfg.ra_password) { throw "ra_username or ra_password is empty in $Json" }

$reply = Invoke-RestMethod -Method Post -Uri 'https://retroachievements.org/dorequest.php' `
    -Body @{ r = 'login2'; u = $cfg.ra_username; p = $cfg.ra_password } -UserAgent 'tico-ra-fix/1.0'
if (-not $reply.Success -or -not $reply.Token) { throw "RetroAchievements refused the login: $($reply.Error)" }

$cfg | Add-Member -NotePropertyName ra_token -NotePropertyValue $reply.Token -Force
$cfg.PSObject.Properties.Remove('ra_password')
[IO.File]::WriteAllText((Resolve-Path -LiteralPath $Json), ($cfg | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
"OK: token saved for $($cfg.ra_username), password removed from the file."
