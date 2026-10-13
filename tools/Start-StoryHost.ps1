$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$gameRoot = Join-Path $taskRoot 'runtimes\enhanced'
$gameExe = Join-Path $gameRoot 'CoFLaunchApp.exe'
$active = Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'"
if ($active) {
    Write-Host 'Cry of Fear is already running. Close the current game before starting this host.'
    exit 1
}
$session = Get-Content -LiteralPath (Join-Path $taskRoot 'state\enhanced-remote-session.json') | ConvertFrom-Json
if ((Get-FileHash -LiteralPath (Join-Path $gameRoot 'xash.dll')).Hash -ne $session.engine_sha256) {
    throw 'The host engine differs from the matching playtester package. Prepare matching builds before hosting.'
}
foreach ($mapName in @('c_start', 'c_start2')) {
    if (Test-Path -LiteralPath (Join-Path $gameRoot ('cryoffear\maps\' + $mapName + '.ent'))) {
        throw 'A development map override is still installed; ordinary campaign start refused.'
    }
}
# Keep every session's log: tools\story_run_report.py adds them up into
# campaign coverage (which exits and maps have actually been proven).
$log = Join-Path $gameRoot 'story-remote-host.log'
if (Test-Path -LiteralPath $log) {
    $archive = Join-Path $gameRoot 'story-logs'
    New-Item -ItemType Directory -Force -Path $archive | Out-Null
    $stamp = (Get-Item -LiteralPath $log).LastWriteTime.ToString('yyyyMMdd-HHmmss')
    Move-Item -LiteralPath $log -Destination (Join-Path $archive "story-host-$stamp.log")
}
Write-Host 'Starting one Cry of Fear story host. Your friend should use the matching JOIN launcher.'
Start-Process -FilePath $gameExe -WorkingDirectory $gameRoot -WindowStyle Normal -ArgumentList '-windowed -width 1280 -height 720 -console -port 27015 -clientport 27018 -log story-remote-host.log -logtime +exec cof_remote_host.cfg +fps_max 60 +bind j cof_warp +map c_start'
