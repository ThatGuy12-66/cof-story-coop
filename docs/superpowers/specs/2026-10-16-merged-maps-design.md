# Merged story maps: design

Date: 2026-10-16. Status: approved for building ("If you're confident in it, I am too. Build it.").

## Purpose

Fewer loading screens in story co-op. In the 2026-10-15 13:11 run a two-player party took 22 level doors in
23 minutes; each costs 3-4 s of loading plus the exit gather (9 s measured). Door-joined story maps are merged
into one map file, stacked in height, so a level door between them becomes an instant in-map move.

What the user decided:
- Scope: the whole campaign, turned on in waves with a playtest each; apartments first (9 of those 22 loads).
- A level door between two sections of one merged map moves each player on their own: instant, no waiting,
  no load; anyone right beside them comes along (as the apartment `inter_door` doors do today).
- A host setting "Merged maps", on by default, can switch it off for a session.
- Approach A: engine sections (the original level-door entities stay; the engine turns their level change
  into a section move).

Success: a full playthrough of every enabled group with no level load at a merged door, the story intact,
merged maps looking exactly like the originals (pixel comparison at fixed spots), host and joiner in step.

Background (evidence in `evidence/map-merge-spike-20261016/`, AGENT_NOTES "Map merge spike"): the throwaway
merger `bspmerge.py`, the offline checker `verify.py`, the live test of `c_apt34` (c_apartment3 +
c_apartment4), and the client.dll renderer limits patch (engine `cof_renderer_limits.c`: 256 lightmap pages,
brush entities to edict 8191), live-verified pixel-identical at 41 spots.

## 1. Merger and setup (launcher side)

**Group list** `tools/map_groups.json` (committed data): per group the merged file name (`cm_` + the first
member without `c_`, e.g. `cm_apartment3`; `[a-z0-9_]{1,32}`), its members in stacking order with each one's
z offset, the SHA-256 of every retail Cry of Fear 1.6 input BSP, the expected SHA-256 of the merged BSP and of
its `.sections` file, a wave number, `enabled`, and accepted differences with a reason (see the planner).

**Merger** `tools/map_merge/` (from the spike's `bspmerge.py`): 2-4 maps per group; members after the first
are moved up by their z offset (multiples of 512, exact texture shifts); origin-brush models are moved by their
entity origin, not their vertices. It merges geometry only: planes, textures (deduplicated by name and bytes),
vertexes, vis (block-diagonal), nodes (new root, world nodes one block), clip hulls (new heads first), leafs
(world leafs first), faces and lighting (lightmaps re-laid where float rounding changes a face box), models,
and face light styles mapped by the style rules. The entity lump of the merged BSP holds only the first
member's `worldspawn` (with the members' `wad` lists joined); entities are composed by the engine (section 2).
It writes `<name>.sections`, a small text file: per member its map name, z offset, first model number, the
targetname renames, the light style map, and its landmarks. `verify.py` becomes the merger's self-check after
every merge (structure, hull walks, point contents, PVS, faces) and a failure aborts that group.

**Setup and update** (`tools/friend_setup.py`, next to the model and art generators in `setup()` and
`update()`): for each enabled group whose input hashes match, merge into `cryoffear/maps/`, check the output
hashes against the group list (identical files for every player, so host and joiner match), and record them in
`cof-story-installed.json` under `merged_maps`; `validate_runtime` checks them like the generated models. A
group whose inputs do not match is skipped and logged and plays unmerged. The member BSPs stay where they are.

**Planner** (`tools/map_merge/plan.py`, dev tool, not run by setup): proposes groups from the door graph and
checks, per group: BSP30 16-bit limits (clipnodes, nodes, faces, marksurfaces, vertexes, texinfo), lightmap
pages <= 256 (engine packer simulation), entity count and precache counts against the engine's limits, the
coordinate range (+-4096), at most 32 switchable light styles, hl.dll's AI node limit (all `info_node`s of the
file build one graph), equal `skyname`, equal fog setup, `worldspawn` keys hl.dll reads (`player1`-`player4`,
`materialstxt`, `MaxRange`, `WaveHeight`, `sounds`) equal or listed as accepted with a reason, `globalname`s
shared between members (refused until handled), maps hl.dll names (`c_city`, `c_forest3`, `c_intro`,
`c_nightmare`, `c_start`, `c_subwaysick1`, `c_ending*`) never merged, and for every level door between two
members: exactly one source and one destination landmark, and its `changetarget`.
*Wave 2 (2026-10-17), from the hl.dll name research and the wave 1 exceptions:* player models never block,
an absent `materialstxt` is the default file, an absent `WaveHeight` is 0, `MaxRange` takes the largest; only
`c_nightmare`, `c_start` (until its easter egg is added) and `c_game_menu1` are never merged; a group with a
`c_ending` member is named `cm_` + the whole first member name (`cm_c_ending4b`), since hl.dll's credits test
looks for `c_ending` in the map name (`docs/research/2026-10-17-merging-the-rest.md`).

## 2. Loading a merged map (engine)

**Switch:** server cvar `cof_merged_maps` (from the host setting, recorded in each session). When on, the
engine reads `maps/*.sections` at map load. A request for an original map that is a member of a merged file
(new game, chapter start, a level door from an unmerged map, Continue) loads the merged file and starts in that
member's section. `sv.name` is the merged file. A player's current section is the member whose height band
holds their origin.

**Entity composition at load** in the shared map loader (`Mod_LoadEntities`, client and server alike), for
any map file that has a `.sections` file (the cvar only steers which file a name loads): `worldspawn` from
the merged BSP's own lump (the first member's, `wad` lists joined); then per member, its entity string exactly
as today (language pack `.ent`, else gamedir `.ent`, else the member BSP's
own lump; then the pack's `.entpatch`), so translations apply to the original names; then the `.sections`
rules: origins up by the z offset, brush models renumbered, clashing targetnames renamed with every key that
refers to them (`target`, `killtarget`, `landmark`, `netname`, `multi_manager` keys, ...), switchable light
styles shifted, `globalname` untouched, every member's own `worldspawn` dropped. The merger computes the
rules (Python); the engine only applies them (C).

**Spawning:** every member's `info_player_start` is kept; a player who spawns or respawns is placed at their
current section's start.

**Keyed on the merged file's name:** the AI node graph (`maps/graphs/<name>.nod`), save snapshots
(`<name>.HL1-3`), the session map history. Story log lines carry the section name.

## 3. Section moves (engine)

**Where:** the engine's change-level entry (`pfnChangeLevel`, `sv_game.c`) gains one check first: when
merging is on and the destination is another section of the current file, it does a section move instead of
the exit hold, save and load; anything else goes on as today.

**Who moves:** the player standing in that exit (the exit-area test the gather uses, which knows the use-only
apartment exits), anyone within 128 units of them, and their AI companions. With nobody in the exit (a
scripted change at the end of a cutscene) the whole party moves, and players far from the leader land beside
the leader (today's arrival rule).

**How:** each mover's origin is shifted by destination landmark - source landmark (renamed names from the
`.sections` file); velocity and view angles are kept; the arrival check fixes overlaps and stuck spots; a
short screen fade like the apartment doors. The exit's `changetarget` fires as the original game would.

**Exits stay open:** the engine remembers every exit area that led to a section move this session and moves
any player who walks into it later the same way (some original exits are once-only, e.g.
`c_apartment3/landfitta`).

**Leaving the group:** a level door to an unmerged map works as today, gather included; "the party is at the
exit" counts players in every section, and J still leaves alone.

**Safety net:** a section move that cannot be worked out at runtime puts the player at the destination
section's start and logs why; it never falls into an impossible level change.

## 4. Per-section behaviour (engine)

- **Autosave** when any player first enters a section in this session (today: after each new map load); the
  sections entered are kept in the session.
- **Sound:** a "play everywhere" sound (`ATTN_NONE`) from an entity in one section is sent only to players in
  that section; attenuated sounds are already culled by the potentially-audible set (the sections' vis is
  block-diagonal). A player entering a section is sent that section's looping sounds that are playing.
- **Story systems** that reset on a new map (party, share, recovery, companion re-add, moments, scene skip)
  keep doing so on a map load only; a section move is not a new map.
- **Companions** follow their owner through section moves; the node graph covers every section.
- **Fog and music** are shared across sections: the planner requires equal fog per group; music is Cry of
  Fear's own shared co-op music (`sv_coop_music`), as in its co-op mode. Revisit after playtests.
- **Monsters** of every section are live at once; vis keeps sections apart. Revisit after playtests.

## 5. Sessions, Continue and the launcher

- `session.dat` gains `merged` (0/1) and `section` (the original map name the party is in); its `map` stays
  the file loaded (`cm_apartment3`), so the existing preflight (`maps/<map>.bsp` exists) holds. Saves from
  before this feature have no `merged` and load unmerged.
- A session keeps its own mode: Continue loads the save's mode whatever the checkbox says; the checkbox
  applies to new games and chapter starts.
- Launcher: host screen checkbox "Merged maps (fewer loading screens)", on by default, saved in settings,
  written as `cof_merged_maps` into the session cfg. Continue shows the `section`. Chapter starts keep
  original map names (the engine resolves them). Joiners need nothing: setup made the same files.
- Run report: section moves are logged (`[cof-story-transition] phase=section-move ... from= to=`) and count
  as maps reached and exits proven.

## 6. Testing and rollout

- Python: merger tests on a small synthetic BSP and on the retail apartment maps (`baseline/`, skipped when
  absent): structure, determinism (same hash twice), the `.sections` file, planner checks; setup tests
  (merge called, hashes recorded, mismatched inputs skipped, `validate_runtime`).
- C harnesses (`tests/engine/`, the existing `run_native` pattern): `.sections` parsing, name resolution,
  entity composition on the retail apartment lumps (checked against invariants the Python side computes:
  entity count, moved origins, every renamed reference resolves, every `*n` in range), the section-move
  decision and offset maths, the open-exit watch, the sound filter decision, session fields.
- Scripted in-game checks (the method of 2026-10-16: `ent_fire 1 set origin`, `fixangle`, `snapshot`): per
  group, pixel comparison merged vs original at many spots; a scripted walk into every door between sections
  with the log checked for `section-move` and the landing point checked against the landmark offset.
- Live playtests per wave with the user (each launch needs the user's go-ahead).
- Wave 1: the apartment groups the planner accepts (c_apartment1, 2, 5, 3, 4 are one door chain); later
  waves follow the story order.

## Out of scope

Merging maps hl.dll names; per-section fog or music; merging the c_game_menu1 menu map; any change to the
original maps on disk.
