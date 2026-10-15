<#
.SYNOPSIS
Starts the disposable campaign test copies on this PC.

.DESCRIPTION
Run tools\Prepare-CampaignTest.ps1 first. Only the copies under evidence\story-live-20261012
(or -Root) are used. The previous logs are kept under <copy>\campaign-logs\ so
tools\story_run_report.py can add them up. Closing this window leaves the games running.

-Mode two        Host "Little Rascal 1" and guest "Little Rascal 2", two windows, two people
                 (or one person switching between them).
-Mode solo       The same two windows side by side, for ONE person: you play the host; the AI
                 plays the guest whenever you leave its window alone (cof_companion_drive). Click
                 into the guest window and move to take it over; 8 s after you stop, the AI has it back.
-Mode companion  Only the host window, with an AI companion bot beside you (cof_companion 1):
                 no second window at all.

-Gun             The test gun every person keeps (cof_test_gun): none (default: the game as it is), m16, shotgun, vp70,
                 revolver, rifle or glock. With a gun on, every gun you
                 carry has infinite ammo (syringes and flares too). It stays with you through level
                 changes, deaths and checkpoints, and comes back after the story empties your
                 inventory, as long as a slot stays free for a key.
#>
param(
    [string]$Map = 'c_intro',
    [ValidateSet('two', 'solo', 'companion')][string]$Mode = 'solo',
    [ValidateSet('m16', 'shotgun', 'vp70', 'revolver', 'rifle', 'glock', 'none')][string]$Gun = 'none',
    [string]$Root
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
if (-not $Root) { $Root = Join-Path $project 'evidence\story-live-20261012' }
$Root = (Resolve-Path -LiteralPath $Root).Path
if ($Map -notmatch '^[a-z0-9_]{1,32}$') { throw 'Map names are lowercase letters, digits and underscores.' }
$roles = if ($Mode -eq 'companion') { @('host') } else { @('host', 'guest') }
foreach ($role in $roles) {
    $cfg = Join-Path $Root "$role\cryoffear\cof_campaign.cfg"
    if (-not (Test-Path -LiteralPath $cfg)) { throw "Run tools\Prepare-CampaignTest.ps1 first ($role is not prepared)." }
    foreach ($fixture in @('c_start', 'c_start2')) {
        if (Test-Path -LiteralPath (Join-Path $Root "$role\cryoffear\maps\$fixture.ent")) { throw "A fixture map override is still installed in $role." }
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $Root "host\cryoffear\maps\$Map.bsp"))) { throw "No map named $Map in the host copy." }
$hostHash = (Get-FileHash -LiteralPath (Join-Path $Root 'host\xash.dll')).Hash
if ($Mode -ne 'companion') {
    $guestHash = (Get-FileHash -LiteralPath (Join-Path $Root 'guest\xash.dll')).Hash
    if ($hostHash -ne $guestHash) { throw 'Host and guest engines differ; run Prepare-CampaignTest.ps1 again.' }
}
$running = @(Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'" |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase) })
if ($running) { throw 'A test copy is already running.' }

Add-Type -Namespace CoFTest -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr hWnd, System.IntPtr after, int x, int y, int cx, int cy, uint flags);
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(System.IntPtr hWnd);
'@

function Save-Log([string]$copy, [string]$name) {
    $log = Join-Path $copy $name
    if (Test-Path -LiteralPath $log) {
        $archive = Join-Path $copy 'campaign-logs'
        New-Item -ItemType Directory -Force -Path $archive | Out-Null
        $when = (Get-Item -LiteralPath $log).LastWriteTime.ToString('yyyyMMdd-HHmmss')
        Move-Item -LiteralPath $log -Destination (Join-Path $archive "$when-$name")
    }
}

# The game window of the copy started from $copy, once it exists (the engine centres it).
function Get-GameWindow([string]$copy, [int]$seconds = 60) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        $proc = Get-Process -Name CoFLaunchApp, cof -ErrorAction SilentlyContinue |
            Where-Object { $_.Path -and $_.Path.StartsWith($copy, [StringComparison]::OrdinalIgnoreCase) -and $_.MainWindowHandle -ne 0 } |
            Select-Object -First 1
        if ($proc) { return $proc.MainWindowHandle }
        Start-Sleep -Milliseconds 500
    } until ((Get-Date) -gt $deadline)
    return [IntPtr]::Zero
}

function Move-GameWindow([IntPtr]$window, [int]$x, [int]$y) {
    # SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
    if ($window -ne [IntPtr]::Zero) { [void][CoFTest.Win]::SetWindowPos($window, [IntPtr]::Zero, $x, $y, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010) }
}

$hostCopy = Join-Path $Root 'host'
$guestCopy = Join-Path $Root 'guest'
Save-Log $hostCopy 'campaign-host.log'
if ($Mode -ne 'companion') { Save-Log $guestCopy 'campaign-guest.log' }

$hostExtra = switch ($Mode) {
    'solo' { '+cof_companion_drive 1 +cof_companion 0' }
    'companion' { '+cof_companion 1 +cof_companion_drive 0' }
    default { '+cof_companion 0 +cof_companion_drive 0' }
}
$hostExtra += if ($Gun -eq 'none') { ' +cof_test_gun 0' } else { " +cof_test_gun weapon_$Gun" }
Write-Host "Starting the host on $Map ($Mode mode, test gun: $Gun, engine $($hostHash.Substring(0, 12)))..."
Start-Process -FilePath (Join-Path $hostCopy 'CoFLaunchApp.exe') -WorkingDirectory $hostCopy -WindowStyle Normal -ArgumentList `
    "-windowed -width 1280 -height 720 -console -port 27115 -clientport 27116 -log campaign-host.log -logtime +exec cof_campaign.cfg $hostExtra +map $Map"
$hostWindow = Get-GameWindow $hostCopy
if ($Mode -eq 'solo') { Move-GameWindow $hostWindow 0 0 }

if ($Mode -ne 'companion') {
    # Give the listen server time to load the map before the guest connects; the guest retries anyway.
    $deadline = (Get-Date).AddSeconds(60)
    do {
        Start-Sleep -Seconds 2
        $up = Get-NetUDPEndpoint -LocalPort 27115 -ErrorAction SilentlyContinue
    } until ($up -or (Get-Date) -gt $deadline)
    Start-Sleep -Seconds 5

    $size = if ($Mode -eq 'solo') { '-width 800 -height 450' } else { '-width 960 -height 540' }
    $quiet = if ($Mode -eq 'solo') { '+volume 0' } else { '' } # one game's sound is enough for one person
    Write-Host 'Starting the guest, joining 127.0.0.1:27115...'
    Start-Process -FilePath (Join-Path $guestCopy 'CoFLaunchApp.exe') -WorkingDirectory $guestCopy -WindowStyle Normal -ArgumentList `
        "-windowed $size -console -port 27119 -clientport 27120 -log campaign-guest.log -logtime +exec cof_campaign.cfg $quiet +connect 127.0.0.1:27115"
    if ($Mode -eq 'solo') {
        $guestWindow = Get-GameWindow $guestCopy
        Add-Type -AssemblyName System.Windows.Forms
        $screen = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
        Move-GameWindow $guestWindow ([Math]::Max(0, $screen.Right - 816)) 0
        # Back to the host: that is the one you play.
        if ($hostWindow -ne [IntPtr]::Zero) { [void][CoFTest.Win]::SetForegroundWindow($hostWindow) }
    }
}

switch ($Mode) {
    'solo' {
        Write-Host 'Solo test: play the host (left). The AI plays the guest (right) while you leave it alone;'
        Write-Host 'click into it and move to take over, and it hands back 8 s after you stop.'
    }
    'companion' { Write-Host 'Companion: an AI partner joins you once the map has loaded. Host console: cof_companion_cmd status.' }
    default { Write-Host 'Both started.' }
}
if ($Gun -ne 'none') {
    Write-Host "Test gun: every person gets the $Gun (once they stand free for 2 s) and never runs out of ammo."
    Write-Host 'Host console: cof_test_gun weapon_shotgun adds that gun too, cof_test_gun 0 turns infinite ammo off.'
}
Write-Host 'J joins your partner (or, waiting at an exit for 10 s, goes alone). Host console: cof_save <slot> / cof_load <slot>.'
Write-Host 'Afterwards: python tools\story_run_report.py evidence\story-live-20261012\host\campaign-host.log'
