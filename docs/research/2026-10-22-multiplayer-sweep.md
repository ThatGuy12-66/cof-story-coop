# Multiplayer sweep: single-player behaviour the story lost (2026-10-22)

The user, after the quick dual wield fix: "Find any other shit like that that we may have missed."

The story runs as a multiplayer game (deathmatch 1), so every `IsMultiplayer()`, `IsDeathmatch()`, `deathmatch`,
`maxClients > 1`, `pmove->multiplayer` and `GetMaxClients() > 1` branch in the retail DLLs takes its multiplayer side,
and every "player 1" or single global assumes one player. Five read-only agents went through every such site of the
decompilation (`caixax/cof-decompiled`, byte-identical to our hl.dll and client.dll) in slices: `player.cpp` (69
sites), world/effects/triggers and the rest of `dlls/` (91), weapons and `client.cpp` (91), the game rules (all 62
vtable slots, single vs multiplayer), and client.dll with `pm_shared`. Each site was checked against the engine's
existing handling. Bytes were checked against the retail hl.dll / client.dll with capstone. Slice reports and scripts:
the session scratchpad (`mpsweep/`), not kept.

## Fixed in 1.2.5

| What players saw | Where | Fix |
|---|---|---|
| Two quick-slot keys at once did not dual-wield | QuickSelectItem / CheckQuickDualWield refuse in MP | gates in `sv_cof_story.c`, deploy in `sv_cof_retail.c` |
| Monsters' claws, bites and swings landed through walls and doors; the taller's stamp killed a knocked-down player behind a wall | `CheckTraceHullAttack` (combat.cpp:1556) and CoF_Taller.cpp:224: in MP also trace each living player's model alone (pfnTraceModel), ignoring the world | `pfnTraceModel` on a player misses when the world or a brush entity comes first (`cof_sp_melee`). The loop stays: MP PreThink makes a player touching another SOLID_NOT, and only it reaches them |
| No limp below 25 health | physics key `low` set only in SP (player.cpp:4143/4166/4188/7550) | gates `limp-on/off/restore`, `cof_sp_limp` |
| No control hints at all (the c_start tutorial, the phone, weapons, a struggle's "mash the buttons") | `ShowHint` returns in MP (player.cpp:14872); a `cof_hint` shows only to its trigger's activator | gate `hints` (100F3DC7), `cof_sp_hints`; `sv_cof_state.c` sends the story's hints to the rest of the party |
| A second syringe too soon never started the morphine trip | `CSyringe::Inject` (CoF_Syringe.cpp:224) | gate `morphine-trip` (1007AAC4: only the player's own syringe trips them, never a partner's heal), `cof_sp_morphine_trip`; the AI companions' trip timer kept 0 |
| Guests' QUICK SELECT pictures empty after every level change or load | the restore's resend runs on one global flag (`m_bRestored` 0x10226476) the first PostThink clears | `sv_cof_arrival.c`: QSlotUpdate at every story sign-on |
| The "new objective" popup only for one player | `showobjective` is one global (effects.cpp:12298, player.cpp:6765) | `sv_cof_state.c`: isNew set for a partner, once |
| Nightmare (difficulty 4) ran the normal stamina | `skillset` sets `m_bNightmare` (P+0x1EE0) only in SP | `SV_CoF_RetailNightmareFrame` |
| Monsters did not hear a hard landing | PostThink 6882 | gate `landing-sound`, `cof_sp_details` |
| A respawn or join blanked everyone's message strip | Spawn's `UTIL_ProMessage(this, " ", 2, true)` | gate `spawn-strip` (all → this player) |
| No Glock brass | CoF_Glock.cpp:485 | gate `glock-brass` |
| Intro card in English for everyone | CCoFIntroduction sends the MP byte; the client then reads `cl_coop_language` | gate `intro-language` |
| A stray Simon health bar in c_ending4b after the fight | CSickSimon::Spawn makes the first monster_booksimon an MP boss | gate `booksimon-boss` (10070AB6 `je`→`jmp`) |
| First-person arms in the police officer's sleeves and hands, never the costume's | client.dll GL_DrawPoints `bMultiplayer = GetMaxClients() > 1` | client gate `simon-arms` (10065F20), `cof_coop_simon_arms` |
| The co-op player list's pings stayed 0 | client.dll holds IN_SCORE only on `cof_` maps; Xash sends pings only then | `SV_ShouldUpdatePing`: once a second in story co-op |
| A guest could stop every monster (`aitest`) or kill c_ending4b's book Simon (`simonmode`) | developer commands with no developer check | `sv_cof_guard.h` refuses them for guests |
| (Dormant) a weapon already owned could not be picked up again with `mp_weaponstay` 1, which the legacy launcher set | multiplay_gamerules.cpp:1985 | `mp_weaponstay 0` in the story config |

## Found and left (reasons)

- **A picked-up weapon is not put in hand** (MP `FShouldSwitchWeapon` switches only to a heavier weapon). Every
  engine re-give (party gifts, chapter and bonus kits, the respawn restore) would draw each weapon in turn; it needs an
  engine-side choice, not the single-player rule.
- **End of the story: the empty co-op stats panel instead of the player's stats, grade and unlock notices**
  (`cof_credits` sends the MP byte; gate candidate 100A37FF `74`→`EB`). The single-player panel's Continue sends
  `closegame`, which does nothing in a multiplayer game: needs a live check of the ending flow first.
- **Endings, costumes and doctor mode unlock only on the host's machine** (`SetEndingOrUnlockables` writes the
  server's `scriptsettings.dat`).
- **Simon's bag rattle is the police equipment jingle; the per-step view bob/roll and the ladder punch are off**
  (`pmove->multiplayer` in pm_shared, predicted on the client too). Setting the flag false in story co-op on both
  sides would restore them; prediction risk, needs a remote-guest test.
- **Monster hits that write `pev->punchangle` directly don't jolt the view** (UpdateClientData sends a zero punch in
  MP; `ViewPunch` itself still arrives through `events/viewpunch.sc`). Cosmetic.
- **The branch's first pickup line and the rifle's first draw**: the single-player path sends a null p-model; needs
  the weapon-model table first.
- **`ironsights_toggle`** is a server cvar: the host's hold/toggle choice is everyone's.
- **Developer commentary** (only with `dev_commentary` and its unlock): the host's Enter skips anyone's; the volume
  ducking is the host's own volume.
- **One ladder global** (`wasqueuedtoreduceorigin`): two simultaneous climbs can offset one camera model for one
  animation. **func_stairs** removed in MP (21 story maps): its tracking keeps only the last player in the box.
- **`cof_clearitems`** clears the activator's quick-slot numbers only; the resend above sends a slot only while its
  weapon name is set, which that entity clears for everyone.
- Not a problem: MOTD (no motd file in the game), `heartbeat` and `v_dark` globals (a frame's slip), the rules'
  Think (no frag/time limit, no intermission or map cycle reachable in the story), mapper mode, teamplay.

## Already handled before this sweep (confirmed)

Carry limits (11 gates; no other MP carry branch), friendly fire, DeathNotice crash, rain_modify, per-player globals,
Simon's voice, black sky, the co-op HUD on story maps, vote-kick, costumes, night vision, donator, fog, held lights,
camera models, cutscene skip, boss bars after a respawn, keypad codes, the one-player sites (crazybitch, roof boss,
doctor, padlock, strangle, wheelchair, puzzle bars, telephone, telescope).
