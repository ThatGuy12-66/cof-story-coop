$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$hostRoot = Join-Path $taskRoot 'runtimes\enhanced'
$guestRoot = Join-Path $taskRoot 'runtimes\enhanced-local-client'
$guestExe = Join-Path $guestRoot 'CoFLaunchApp.exe'
$hostExe = Join-Path $hostRoot 'CoFLaunchApp.exe'

$running = @(Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'")
if (-not ($running | Where-Object { $_.ExecutablePath -eq $hostExe })) {
    Write-Host 'Start the story host first (HOST-STORY.cmd), then run this guest.'
    exit 1
}
if ($running | Where-Object { $_.ExecutablePath -eq $guestExe }) {
    Write-Host 'The local guest copy is already running.'
    exit 1
}
$session = Get-Content -LiteralPath (Join-Path $taskRoot 'state\enhanced-remote-session.json') | ConvertFrom-Json
if ((Get-FileHash -LiteralPath (Join-Path $guestRoot 'xash.dll')).Hash -ne $session.engine_sha256) {
    throw 'The guest engine differs from the host session engine. Copy the matching xash.dll first.'
}

# Same settings the playtester JOIN window writes, with the host's own server password (never printed).
$hostCfg = Get-Content -LiteralPath (Join-Path $hostRoot 'cryoffear\cof_remote_host.cfg')
$pwLine = $hostCfg | Where-Object { $_ -match '^\s*sv_password\s+"([^"]*)"' } | Select-Object -First 1
if (-not $pwLine) { throw 'sv_password not found in the host config.' }
$null = $pwLine -match '^\s*sv_password\s+"([^"]*)"'
$joinCfg = @(
    'name "Little Rascal 2"'
    # Re-setting an unchanged cvar is a no-op; blank first so userinfo refreshes.
    'password ""'
    ('password "' + $Matches[1] + '"')
    'fps_max 60', 'rate 50000', 'cl_cmdrate 60', 'cl_updaterate 60'
    'cl_forwardspeed 300', 'cl_backspeed 200', 'cl_sidespeed 200', 'sensitivity 4', '+mlook'
    'bind j cof_warp'
) -join "`n"
[IO.File]::WriteAllText((Join-Path $guestRoot 'cryoffear\cof_local_join.cfg'), $joinCfg + "`n")

Write-Host 'Starting local guest "Little Rascal 2" -> 127.0.0.1:27015'
Start-Process -FilePath $guestExe -WorkingDirectory $guestRoot -WindowStyle Normal -ArgumentList '-windowed -width 960 -height 540 -console -port 27019 -clientport 27020 -log story-local-guest.log -logtime +exec cof_local_join.cfg +connect 127.0.0.1:27015'
