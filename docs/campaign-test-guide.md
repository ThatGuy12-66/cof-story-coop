# Campaign test guide (two players on this PC)

This is the checklist for the next live test of the story campaign in co-op. It uses the two
disposable runtime copies under `evidence/story-live-20261012/` (host and guest, isolated from your
normal `runtimes/enhanced` copy and its saves).

## Start

With both test games closed, in PowerShell from the project folder:

```powershell
# Only after a new build; both hashes from that build (xash.dll and vgui.dll are installed together):
# .\tools\Prepare-CampaignTest.ps1 -ExpectedSha256 <xash.dll SHA-256> -ExpectedVguiSha256 <vgui.dll SHA-256>
.\tools\Start-CampaignTest.ps1                   # solo mode, starts at c_intro; -Map c_start skips the intro
.\tools\Start-CampaignTest.ps1 -Mode companion   # one window, an AI companion bot beside you
.\tools\Start-CampaignTest.ps1 -Gun shotgun      # any mode: a test gun (default none: off)
```

`Prepare` installs the newest engine and its `vgui.dll` (only if both hashes match), retires the `c_start`/`c_start2`
test-fixture overrides (moved aside, not deleted) and writes `cof_campaign.cfg` with cheats off.
`Start` has three modes:

| Mode | Windows | Who plays what |
|---|---|---|
| `-Mode solo` (default) | host (left) and guest (right, 800×450, muted) | You play the host. The AI plays the guest whenever you leave its window alone; click into it and move to take it over, and the AI has it back 8 s after you stop. Tests both clients with one person. |
| `-Mode companion` | host only | You play; an AI companion bot joins once the map has loaded (see `docs/companion.md`). |
| `-Mode two` | host (1280×720) and guest (960×540) | Two people, or one person switching windows. |

**H** gives the AI orders (wait here / follow me) in solo and companion modes.

**Enter skips a cutscene** for everyone (`cof_skip`, either window). The game runs about ten times
faster until the scene is over (`cof_skip_speed`, default 10). Every scripted event still happens, in
order, only sooner, so nothing the story sets up is missed. It works only while everyone alive is
frozen in the scene: nobody ever plays at that speed. Press Enter again to watch the rest. The
3-minute intro then takes about 20 seconds. Sounds overlap while it runs. A new map's cutscene is
not skipped until someone presses Enter again.

**The test gun** (`-Gun`, every mode): off by default; `-Gun m16` gives each person an M16 (`-Gun shotgun`, `vp70`,
`revolver`, `rifle` or `glock` for another). The gun arrives once you
have stood free for 2 seconds and goes straight into your hands. Every gun you carry then has
infinite ammo and never needs a reload, and syringes and flares are never used up either. The gun
stays with you through level changes, deaths and checkpoints. When the story empties your
inventory (`c_start`, the `c_bridge` bag, the stairway), it comes back. It is handed over only while
two slots are free, so a key always fits; drop something if you want it back sooner. A gun you drop
stays on the floor and a new one is handed over. The AI companion bot keeps its own kit. Host
console: `cof_test_gun weapon_shotgun` hands out a shotgun as well (the M16 stays),
`cof_test_gun 0` turns infinite ammo off (the log says `test-gun-off`).

Use `-Gun none` for the checks where ammo matters: the AI reloading a story gun (in solo mode the
driven guest otherwise holds the M16 and never reloads), ammo pickups, a full inventory, and the
inventory brought back after a death or a checkpoint load.

## What should happen

| Situation | Expected behaviour |
|---|---|
| A cutscene starts | Both players watch it, frozen, safe from monsters. It ends for both. |
| One player dead during a cutscene, revived afterwards | They can move and see normally again (`camera-released` in the log). |
| Respawn or rejoin during a cutscene | They watch it frozen like the other (`camera-held`). |
| A cutscene teleports the player it is about | The partner who watched it lands beside them, or on the same spot when the room only fits one (the intro's last room, `kunitei`). |
| Enter during a cutscene | "<name> skips the cutscene"; it runs fast to its end (`cutscene-skip`, then `cutscene-skip-over`). |
| The intro's logos and credits | Both players see them (`logo-shared`). |
| Exit while the partner is elsewhere | The level waits; the partner presses **J** to join; after 10 s the waiter's J goes alone. In solo mode a guest the AI is playing does not hold the exit: it comes along and lands beside you. |
| Everyone dead, a body lying in an exit | The exit is held, never taken by the dead; the checkpoint loads (`session-wipe-recovery`) no sooner than 2 s into a map, and never while a player is still connecting. |
| J | Join your partner. Refused while you are frozen (cutscene, ladder, puzzle view). |
| Death | Press jump after **10 s** to respawn beside your partner with your items, or get revived. |
| Falling into a death pit (subway, endings) | Only the one who fell dies (`fall-death`). |
| Someone climbs a ladder | The other cannot start the same ladder until they are up ("is climbing"). |
| A key, fuse or card is found | Both carry it; using it removes it from both. |
| Glock, flashlight, camera, shotgun handed out by the story | Both players get one. |
| Notes, codes, objectives | Shared. Either player can type any code. |
| Phone call (c_basement, c_park3) | Rings a player who has a phone; if it is dropped it rings again. The park gate still opens. |
| A yes/no question (P345, roof escape) | Only the player asked can answer; the first answer counts. |
| The P345 is given to the doctor by either player | The doctor fight in `c_attic` starts normally. |
| The ending door in `c_village3` | Both players black out and arrive in the same ending room; one level change for both. |
| Wardrobe (c_start, c_bridge) | The player who pressed it gets the clothes menu. |
| Tape recorder | Saves a co-op checkpoint (not during a cutscene). The host's console: `cof_load tape1`. |
| Test gun on (default) | Each person has the M16 in hand a few seconds after first standing free (`test-gun-given`); it never empties; after a level change or a respawn it is still there and not given twice. |
| The story empties the inventory (c_start, c_bridge bag) | The test gun comes back about 2 s later; the story's own items still fit. |

### The AI (solo and companion modes)

| Situation | Expected behaviour (log phase) |
|---|---|
| Map loaded | Companion mode: the bot joins beside you (`companion-added`), with a flashlight beam on only if `cof_coop_flashlight 1`. |
| Level change | It leaves with the old map and rejoins in the new one; never two bots, never a stuck slot. |
| You walk off | It follows your path; lost or stuck, it reappears behind you (`companion-teleport reason=far/lost/stuck/trail`). |
| You walk slowly | It walks with you without stopping and starting (few `companion-mode` lines). |
| An ordinary door shuts between you | It opens it (`companion-door`); a locked or scripted door stays shut and it reappears instead. |
| It walks along | No hopping; a jump only over a low obstacle (`companion-jump reason=ledge`). |
| A wall or furniture between it and you | It walks round it (`companion-nav map=… nodes=…` once per map, `companion-route … nodes=N`); a map without a graph says `graph=none`. Stuck on a route it tries another (`companion-replan`) before reappearing. |
| A story weapon is handed out | It takes it in hand (`companion-equip`) and uses it: raised sights at range, single shots, reloads. |
| A monster in sight near you | It fights (`mode=fight`); never with you in the line of fire; it leaves friendly/scripted actors alone. |
| You are down | "Companion is coming to help you up."; it does CPR (`companion-cpr`); you stand with 30 health. |
| You are at half health or less, all quiet | It walks up and gives you a syringe (`mode=medic`), at most every 90 s. |
| It walks over an exit, a scripted trigger or an item | Nothing happens: only you set off the story. |
| A monster grabs it with a gun | It struggles free (`mode=struggle`). |

## 12-slot inventory (engine with the 2026-10-14 inventory build)

Story co-op only; `cof_inventory_slots 6` on the host gives the retail six. Host console: `cof_inventory12_status`, `cof_story_status` (13 `inventory-slot` lines per player).
The copies are prepared with `.\tools\Prepare-CampaignTest.ps1 -ExpectedSha256 <xash.dll hash> -ExpectedVguiSha256 <vgui.dll hash>`,
both hashes from the same build: `xash.dll` and `vgui.dll` share an interface that every engine build changes, so they are
always installed together.

1. `cof_inventory12_status` reads `slots=12 cvar=12 keep=1 wide=1 reason=none`; the host log has `cofstory phase=inventory12 slots=12` for each map.
2. Fill all 12 boxes (the test gun plus story items). The inventory shows a 4x3 grid: BAG over two rows, POCKETS over the third. Each pickup into slots 7..12 shows "You got the ...".
3. Equip a weapon in slot 7..12; use a key from slot 7..12 on its door; drop an item from slot 9; combine the Glock with the tactical light across slots 6 and 7; assign quick select 2 from slot 9: the picture appears under QUICK SELECT right after choosing 1/2/3.
4. Open an item's menu, then click the box next to it: that box's menu opens (the old slot-6 bug).
5. Level change, death and respawn, a checkpoint save and load (`cof_save test12` / `cof_load test12`), and an autosave from before this build: slots 7..12 are kept, the old save loads with them empty.
6. Run `cof_inventory12_status` before and after a full clear (`c_start`): every `inventory12-fields` value is the same, and `dead=0` (the bytes after the slot array). If a value differs, repeat the same move with `cof_inventory_slots 6` on the host: a difference there too is the game's own reset, not the 12-slot patch.
7. A copy still on the previous engine joins a map at 12: refused with "This game uses the 12-slot inventory." Connected before a map loads: that map runs at six (`reason=client:<userid>`), slots 7..12 come back on the next map at 12.
   To get the older client, with both games closed, in ONE copy only (e.g. `guest`): rename the two backups from
   before this build over the installed files, `xash.dll.<hash8>.bak` over `xash.dll` and `vgui.dll.<hash8>.bak` over
   `vgui.dll` (`<hash8>`: the first 8 characters of that file's SHA-256; until this build the copies held `xash.dll`
   `3D860BC7…` and `vgui.dll` `F77963A0…`). Both files, never one: they belong together.
   `tools\Start-CampaignTest.ps1` refuses a host and a guest copy with different engines, so it cannot start this
   pair. Start the two games by hand with the same command lines the script uses, host first (the PowerShell
   window's current folder does not matter; each game runs from its own folder):
   - Host, from `evidence\story-live-20261012\host`:
     `.\CoFLaunchApp.exe -windowed -width 1280 -height 720 -console -port 27115 -clientport 27116 -log campaign-host.log -logtime +exec cof_campaign.cfg +cof_test_gun weapon_m16 +map c_intro`
     (`weapon_m16` and `c_intro` are the script's defaults; change the map if you want to test another).
   - Guest, from `evidence\story-live-20261012\guest`, after the host's map has loaded:
     `.\CoFLaunchApp.exe -windowed -width 960 -height 540 -console -port 27119 -clientport 27120 -log campaign-guest.log -logtime +exec cof_campaign.cfg +connect 127.0.0.1:27115`
   Afterwards close both games and run `Prepare-CampaignTest.ps1` again with the same arguments to put this build
   back into that copy.
8. `c_nightmare`: the inventory still does not open (the game's own rule).
9. A weapon in slot 9 survives a level change and a death/respawn, and can still be equipped afterwards.

## If something goes wrong

- Note the map, roughly what happened, and who was host or guest.
- Leave the games running if a player is stuck; the host console can still run `cof_story_status`.
- Afterwards: `python tools\story_run_report.py evidence\story-live-20261012\host\campaign-host.log`
  summarises transitions and warnings. Logs can contain the RCON password; redact before sharing.

## Not yet handled (expected rough edges)

- Some effects still reach only one player: the `c_village3` look-at, the `c_ending4` grey fade,
  camera zooms and blur toggles.
- `c_asylumb` puzzle: a guest who solves it may need to press the puzzle's close button.
- `c_ending1c–3c` strangle: only the host's attack input finishes it.
- Dropped weapons saved in `c_college1` come back only for the player who saved them.
- A skipped cutscene's music and voices play over each other while it runs fast.
