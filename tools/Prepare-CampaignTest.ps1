<#
.SYNOPSIS
Turns the two disposable runtime copies (host + guest) into clean story-campaign test copies.

.DESCRIPTION
Works only on the isolated copies under evidence\story-live-20261012 (or -Root); the normal
runtimes\enhanced and runtimes\enhanced-local-client copies, saves and configs are never touched.
Nothing is deleted: fixture map overrides and the previous engine are moved/copied aside.

- Refuses while either copy's game is running.
- Installs the engine only if its SHA-256 matches -ExpectedSha256, and always together with its
  vgui.dll (FreeVGUI, -Vgui), only if that matches -ExpectedVguiSha256. Both are required: the two
  share vguiapi_t, which every engine build changes, so xash.dll is never installed without the
  vgui.dll from the same build.
- Generates the 12-slot inventory's grid art from each copy's own retail images (tools\make_inventory12_art.py).
- Retires the c_start/c_start2 fixture .ent overrides (they replace campaign entities).
- Writes cryoffear\cof_campaign.cfg for each copy: story co-op on, cheats off, J = cof_warp,
  H = cof_companion_order, Enter = cof_skip (skip a cutscene for the party).
  The server password is read from the copy's private-session.json and never printed.
Launch afterwards with tools\Start-CampaignTest.ps1.
#>
param(
    [Parameter(Mandatory = $true)][string]$ExpectedSha256,
    [string]$Engine,
    [string]$Vgui,
    [Parameter(Mandatory = $true)][string]$ExpectedVguiSha256,
    [string]$Root
)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
if (-not $Engine) { $Engine = Join-Path $project 'upstream\cof-enhanced\xash3d-fwgs-4857b389e6ba32ddaa68582aedcbc950c138f46a\build-local-20260927\engine\xash.dll' }
if (-not $Vgui) { $Vgui = Join-Path (Split-Path -Parent (Split-Path -Parent $Engine)) '3rdparty\freevgui\vgui.dll' }
if (-not $Root) { $Root = Join-Path $project 'evidence\story-live-20261012' }
$Root = (Resolve-Path -LiteralPath $Root).Path
if ($Root -notlike (Join-Path $project 'evidence\*')) { throw 'Only disposable copies under evidence\ are prepared by this script.' }

$hash = (Get-FileHash -LiteralPath $Engine).Hash
if ($hash -ne $ExpectedSha256.ToUpperInvariant()) { throw "Engine hash $hash does not match the expected build." }
$vguiHash = (Get-FileHash -LiteralPath $Vgui).Hash
if ($vguiHash -ne $ExpectedVguiSha256.ToUpperInvariant()) { throw "vgui.dll hash $vguiHash does not match the expected build." }

$roles = @('host', 'guest')
foreach ($role in $roles) {
    $exe = Join-Path $Root "$role\CoFLaunchApp.exe"
    if (-not (Test-Path -LiteralPath $exe)) { throw "Missing disposable copy: $role" }
}
$running = @(Get-CimInstance Win32_Process -Filter "Name='CoFLaunchApp.exe' OR Name='cof.exe'" |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($Root, [StringComparison]::OrdinalIgnoreCase) })
if ($running) { throw 'A disposable copy is running. Close both test games first.' }

$session = Get-Content -LiteralPath (Join-Path $Root 'private-session.json') | ConvertFrom-Json
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$retired = Join-Path $Root "retired-$stamp"

foreach ($role in $roles) {
    $copy = Join-Path $Root $role
    # Fixture overrides replace campaign entities in c_start/c_start2: retire them.
    foreach ($map in @('c_start', 'c_start2')) {
        $ent = Join-Path $copy "cryoffear\maps\$map.ent"
        if (Test-Path -LiteralPath $ent) {
            $dest = Join-Path $retired "$role\maps"
            New-Item -ItemType Directory -Force -Path $dest | Out-Null
            Move-Item -LiteralPath $ent -Destination (Join-Path $dest "$map.ent")
        }
    }
    # Keep the previous engine beside the copy, named by its hash.
    $installed = Join-Path $copy 'xash.dll'
    $old = (Get-FileHash -LiteralPath $installed).Hash
    if ($old -ne $hash) {
        $backup = Join-Path $copy ("xash.dll.{0}.bak" -f $old.Substring(0, 8))
        if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $installed -Destination $backup }
        Copy-Item -LiteralPath $Engine -Destination $installed -Force
    }
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $hash) { throw "Engine copy failed: $role" }
    # The vgui.dll of the same build, the previous one kept beside it the same way.
    $installedVgui = Join-Path $copy 'vgui.dll'
    $oldVgui = (Get-FileHash -LiteralPath $installedVgui).Hash
    if ($oldVgui -ne $vguiHash) {
        $backup = Join-Path $copy ("vgui.dll.{0}.bak" -f $oldVgui.Substring(0, 8))
        if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $installedVgui -Destination $backup }
        Copy-Item -LiteralPath $Vgui -Destination $installedVgui -Force
    }
    if ((Get-FileHash -LiteralPath $installedVgui).Hash -ne $vguiHash) { throw "vgui.dll copy failed: $role" }
    # The 12-slot inventory's grid art, from this copy's own retail images
    Push-Location $project
    try { & python -m tools.make_inventory12_art $copy | Out-Null } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw "Inventory art generation failed: $role" }

    $number = if ($role -eq 'host') { 1 } else { 2 }
    $settings = @(
        "name `"Little Rascal $number`"", 'fps_max 60', 'rate 50000', 'cl_cmdrate 60', 'cl_updaterate 60',
        'cl_forwardspeed 300', 'cl_backspeed 200', 'cl_sidespeed 200', 'sensitivity 4', '+mlook',
        'bind j cof_warp', 'bind h cof_companion_order', 'bind enter cof_skip', 'bind kp_enter cof_skip',
        'sv_lan 1', 'volume 0.4',
        'cof_story_coop 1', 'cof_story_trace 1', 'sv_cheats 0', 'sv_enttools_enable 0',
        'coop 0', 'deathmatch 1', 'maxplayers 2', 'mp_timelimit 0', 'mp_fraglimit 0',
        ('password "{0}"' -f $session.password)
    )
    if ($role -eq 'host') {
        $settings += @(('rcon_password "{0}"' -f $session.rcon_password), ('sv_password "{0}"' -f $session.password),
            'hostname "Two little rascals - campaign test"')
    }
    [IO.File]::WriteAllText((Join-Path $copy 'cryoffear\cof_campaign.cfg'), ($settings -join "`n") + "`n")
}
"Prepared host and guest campaign copies with engine $hash and vgui.dll $vguiHash."
if (Test-Path -LiteralPath $retired) { "Fixture overrides moved to $retired." }
