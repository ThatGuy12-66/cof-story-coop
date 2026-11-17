# Merged Story Maps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans (the user's choice: inline, no
> reviewer subagents) to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Door-joined story maps merged into one map file at setup, so a level door between them becomes an
instant in-map move ("section move"), turned on in waves starting with the apartments.

**Architecture:** A Python merger (`tools/map_merge/`) builds merged BSPs (geometry only) and `.sections`
rule files from the player's own retail maps during setup. The engine composes the merged map's entities at
load from each member's own entity source and the rules, resolves original map names to merged files, and
turns a change-level request between two sections of the loaded file into a section move.

**Tech Stack:** Python 3 (stdlib only, frozen into the app by PyInstaller), C (Xash3D FWGS fork, MSVC x86),
unittest, native C harnesses (`tests/native.py` `run_native`).

**Spec:** `docs/superpowers/specs/2026-10-16-merged-maps-design.md`

**Rulings recorded at planning time:**
- Ruling: rolling-wave detail. Phase A (merger) is written out in full; phases B-G carry their files,
  interfaces and test cases, and their steps get code when the phase starts (engine code must be read in place
  first). Cost if wrong: a later phase's interface changes and earlier tasks are touched again.
- Ruling: groups of 3-4 maps are built as successive pair merges ((A+B)+C), each pair checked by the
  spike's verifier logic, because a pair merge's output is itself a valid input (world nodes, faces and leafs
  stay first). Cost if wrong: a third member needs a direct N-way merge.
- Ruling: renames use the suffix `_m<k>` (k = member index, 1-based for the second member) and only in
  reference keys (list in Task A3); the planner reports any other key whose value equals a renamed name.
- Ruling: switchable light styles that clash are remapped into free numbers 32-60; 61-63 are Paranoia's
  bump-map styles and are never assigned.

## Global Constraints

- Retail input: Cry of Fear 1.6 maps; every group lists the SHA-256 of each input BSP; mismatched inputs skip
  the group (it plays unmerged).
- Merged file names `cm_` + first member without `c_`, matching `[a-z0-9_]{1,32}`.
- z offsets are multiples of 512; everything stays within +-4096 (entity origins travel as 16 bits / 8).
- Output is deterministic: the same inputs give byte-identical BSP and `.sections` files on every computer.
- Never merge maps hl.dll names: `c_city`, `c_forest3`, `c_intro`, `c_nightmare`, `c_start`, `c_subwaysick1`,
  `c_ending*`; never `c_game_menu1`.
- Lightmap pages <= 256 (the renderer patch); BSP30 16-bit limits; at most 32 switchable styles (32-60 free).
- Original map files on disk are never changed; merged files are new files in `cryoffear/maps/`.
- Engine C files keep LF line endings (`.gitattributes`); launcher repo files CRLF as committed.
- No game launch without the user's go-ahead.

## Review Focus

1. A player's install whose map differs (other language, modded) -> that group is skipped, the rest merge,
   setup succeeds (Task B1 test `test_mismatched_member_skips_group`).
2. Two members both define the same targetname and some entity in the second member refers to it in a key
   outside the reference list -> the planner reports it (Task A6 test `test_unlisted_reference_reported`).
3. A once-only exit used by the first player -> the second player still gets through (Task D3 test).
4. Continue of a save made before this feature, with the checkbox on -> loads unmerged (Task F1 test).
5. A "play everywhere" sound in one section -> not heard by a player in another section (Task E2 test).

---

## Phase A: the merger (`tools/map_merge/`)

Source to port: `evidence/map-merge-spike-20261016/bspmerge.py` and `verify.py` (git-ignored, on this
machine). Test maps: `baseline/Cry of Fear/cryoffear/maps/` (tests skip when absent).

### Task A1: BSP reading and writing

**Files:**
- Create: `tools/map_merge/__init__.py` (empty), `tools/map_merge/bsp.py`
- Test: `tests/test_map_merge_bsp.py`

**Interfaces:**
- Produces: `Bsp.from_bytes(data: bytes) -> Bsp`, `Bsp.read(path) -> Bsp` with list attributes `planes`,
  `vertexes`, `nodes`, `texinfo`, `faces`, `clipnodes`, `leafs`, `marksurfaces`, `edges`, `surfedges`,
  `models` (lists of lists), `textures` (list of mip bytes or None), `vis`, `lighting` (bytes), `ents` (list of
  `[key, value]` lists); `to_lumps(bsp) -> dict` (name -> rows or bytes), `write_bytes(lumps: dict) -> bytes`;
  helpers `parse_ents(text)`, `serialize_ents(ents) -> bytes`, `ent_get(e, key, default=None)`,
  `tex_name(mip)`, `build_textures(mips) -> bytes`, `decompress_vis`, `compress_vis`, `bits_of`, `f32`,
  `face_box(vertexes, edges, surfedges, face, texinfo)`, `relay(src, old, new, nstyles)`, `styles_of(face)`,
  `NAMES`, `FMT`, `WORLD_HALF = 4096.0`.

- [ ] **Step 1: failing tests** — `test_round_trip_keeps_every_lump`: read `c_apartment5.bsp`, `to_lumps`,
  `write_bytes`, `from_bytes` again; every list lump equal, `vis`, `lighting`, `textures`, entity text equal.
  `test_entity_text_round_trip`: `parse_ents(serialize_ents(ents)[:-1].decode('latin-1')) == ents` on the same
  map. Skip when the baseline map is missing.
- [ ] **Step 2:** run `python -m unittest tests.test_map_merge_bsp` -> fails (no module).
- [ ] **Step 3:** port `Bsp`, the lump tables and helpers from the spike (`bspmerge.py` lines 1-190 and
  `write`), with `from_bytes`, and `write_bytes` returning bytes instead of writing a file.
- [ ] **Step 4:** tests pass.
- [ ] **Step 5:** commit `Map merge: BSP30 reading and writing`.

### Task A2: pair merge (geometry only)

**Files:**
- Create: `tools/map_merge/merge.py`
- Test: `tests/test_map_merge_pair.py`

**Interfaces:**
- Consumes: Task A1.
- Produces: `merge_pair(a: Bsp, b: Bsp, dz: float, style_map: dict[int, int]) -> tuple[dict, PairInfo]`
  (lumps dict ready for `write_bytes`; entity lump = `a`'s worldspawn with `b`'s `wad` entries appended);
  `PairInfo` dataclass: `dz`, `split`, `Aw`, `Af`, `Bw`, `Bf`, `Av`, `Bv`, `An`, `Bn`, `a_models`,
  `lightmaps_relaid`, `textures_added`; `choose_dz(a_bounds_z, b_bounds_z) -> float` (smallest multiple of 512
  that leaves a 128-unit gap and stays within +-4096, else ValueError).
- `style_map` maps b's face styles (32-60) to new numbers before writing faces.

- [ ] **Step 1: failing tests** — on `c_apartment3` + `c_apartment4` (baseline): `merge_pair` with
  `choose_dz` gives `dz == 2048.0` (as the spike); `PairInfo.lightmaps_relaid == 119`; the written bytes read
  back; entity lump holds one `worldspawn` whose `wad` contains both maps' wad entries; a face of b with style
  32 written as the mapped style when `style_map={32: 33}`. Determinism: two runs give identical bytes.
- [ ] **Step 2:** run, fails.
- [ ] **Step 3:** port `merge` from the spike (lines 202-448) minus the entity block; take `dz` as an
  argument; apply `style_map` to b's face styles; build the worldspawn-only entity lump.
- [ ] **Step 4:** pass. **Step 5:** commit `Map merge: pair merge, geometry only`.

### Task A3: section rules and the `.sections` file

**Files:**
- Create: `tools/map_merge/rules.py`
- Test: `tests/test_map_merge_rules.py`

**Interfaces:**
- Produces: `REF_KEYS = ('target', 'killtarget', 'targetname', 'landmark', 'changetarget', 'master',
  'netname', 'parentname', 'LightningStart', 'LightningEnd', 'm_iszEntity', 'TriggerTarget')`;
  `PATH_MESSAGE_CLASSES = ('path_corner', 'path_track')` (their `message` is a target);
  `Section` dataclass: `map: str`, `dz: float`, `first_model: int`, `models: int`, `renames: dict[str, str]`,
  `styles: dict[int, int]`, `landmarks: list[str]`;
  `section_rules(members: list[tuple[str, list]], dzs: list[float], model_counts: list[int]) -> list[Section]`
  (members = (map name, entity list)); `to_text(name, sections) -> str`; `parse(text) -> tuple[str, list[Section]]`.
- `.sections` format (ASCII, one record per line, `#` comments, written in member order):
  ```
  cof-sections 1 cm_apartment3
  section c_apartment3 0 1 207
  section c_apartment4 2048 208 193
  rename land1 land1_m1
  style 32 33
  landmark land1_m1
  ```
  `section <map> <dz> <first_model> <model_count>` opens a member; `rename`, `style`, `landmark` lines belong
  to the last `section`. `landmark` lists the member's `info_landmark` targetnames after renames.

- [ ] **Step 1: failing tests** — synthetic entity lists: a name in members 0 and 1 is renamed in member 1 to
  `name_m1`; a name in members 0, 1 and 2 gets `_m1` and `_m2`; a rename never collides with an existing name
  (`a` and `a_m1` already used -> `a_m1_m1`... rule: append `_m<k>` until unused); a light style 32 used by
  members 0 and 1 maps to the lowest free number in 32-60; 61-63 never assigned; more than 29 switchable
  styles -> ValueError; `parse(to_text(...))` round-trips; the real c_apartment3 + 4 pair gives the 11
  renames the spike found (`clash` in `evidence/.../c_apt34.json`).
- [ ] **Step 2:** fails. **Step 3:** implement. **Step 4:** pass. **Step 5:** commit
  `Map merge: section rules and the .sections file`.

### Task A4: pair check (the spike's verifier)

**Files:**
- Create: `tools/map_merge/check.py`
- Test: `tests/test_map_merge_check.py`

**Interfaces:**
- Consumes: A1, A2.
- Produces: `check_pair(a: Bsp, b: Bsp, merged: Bsp, info: PairInfo, points: int = 4000, seed: int = 1)
  -> list[str]` (problems; empty when clean): structure lint, hull ranges, point contents in hull 0-3 at
  `points` random points plus point-entity origins, PVS per leaf, face texture/styles/light (styles compared
  through the style map), re-laid light.

- [ ] **Step 1: failing tests** — the c_apartment3 + 4 pair checks clean; a corrupted copy (one node child
  pointing past the end; one face's light offset moved) reports a problem each.
- [ ] **Step 2:** fails. **Step 3:** port `verify.py` sections 1-4 as functions. **Step 4:** pass.
  **Step 5:** commit `Map merge: pair check`.

### Task A5: groups and building a group

**Files:**
- Create: `tools/map_merge/groups.py`, `tools/map_groups.json`
- Test: `tests/test_map_merge_groups.py`

**Interfaces:**
- Consumes: A1-A4.
- Produces: `Group` dataclass (`name`, `members: list[str]`, `inputs: dict[str, str]` map -> sha256,
  `bsp_sha256`, `sections_sha256`, `wave: int`, `enabled: bool`, `accepted: dict[str, str]`);
  `load_groups(path=GROUPS_JSON) -> list[Group]`; `build_group(maps_dir: Path, group: Group) -> tuple[bytes,
  str]` (BSP bytes, `.sections` text; raises `GroupInputMismatch` when an input hash differs, `MergeCheckFailed`
  when a pair check fails); `install_group(maps_dir, group) -> dict[str, str]` (writes `<name>.bsp` and
  `<name>.sections` through `.partial` + `os.replace`, checks both hashes against the group, returns
  `{'maps/<name>.bsp': sha, 'maps/<name>.sections': sha}`).
- `map_groups.json`: `{"schema": 1, "groups": [{"name", "members", "inputs", "bsp_sha256",
  "sections_sha256", "wave", "enabled", "accepted"}]}`.

- [ ] **Step 1: failing tests** — a group of c_apartment3 + c_apartment4 built twice gives identical bytes;
  `install_group` into a temp maps folder writes both files and returns their hashes; a changed input byte ->
  `GroupInputMismatch`; a wrong expected hash -> `RuntimeError` and no file left behind; `load_groups` rejects
  a bad name (`CM_X`, 33 chars, a member named by hl.dll).
- [ ] **Step 2:** fails. **Step 3:** implement (stack members: dz of member k from `choose_dz` against the
  merged-so-far bounds; style maps and renames from `section_rules`; `check_pair` after every pair).
  **Step 4:** pass. **Step 5:** commit `Map merge: groups, building and installing a group`.

### Task A6: planner

**Files:**
- Create: `tools/map_merge/plan.py` (CLI: `python -m tools.map_merge.plan MAPS_DIR [group-members...]`)
- Test: `tests/test_map_merge_plan.py`

**Interfaces:**
- Produces: `plan_group(maps_dir, members) -> PlanReport` (`ok: bool`, `problems: list[str]`,
  `notes: list[str]`, `numbers: dict`); `door_graph(maps_dir, maps) -> dict[str, set[str]]`.
- Checks (spec section 1): BSP30 counts (clipnodes, nodes <= 32767; faces, marksurfaces, vertexes, texinfo,
  planes <= 65535), lightmap pages <= 256 (port `lm_pages.py`), entities <= 2048 and models <= 1024
  (re-check against the engine's `MAX_EDICTS`/`MAX_MODELS` when Phase C starts), coordinates within +-4096,
  switchable styles <= 29 after mapping, `info_node` total <= 1024 (hl.dll `MAX_NODES`; re-check by RE in
  Phase C), equal `skyname`, equal `env_fog` key/values, worldspawn keys `player1`-`player4`, `materialstxt`,
  `MaxRange`, `WaveHeight`, `sounds` equal unless in `accepted`, no shared `globalname`, no hl.dll-named map,
  every door between members has one source and one destination landmark, `changetarget` noted, and keys
  outside `REF_KEYS` whose value equals a renamed name reported.

- [ ] **Step 1: failing tests** — c_apartment3+4+5 plan ok (with the worldspawn difference reported as a
  problem unless accepted); a plan with `c_start` refused; `test_unlisted_reference_reported` (synthetic:
  member 1 has `"enemyname" "door1"` and `door1` is renamed -> a problem naming the key).
- [ ] **Step 2:** fails. **Step 3:** implement. **Step 4:** pass. **Step 5:** commit
  `Map merge: planner`.

### Task A7: wave 1 groups

- [ ] Run the planner over the apartment chain (`c_apartment1`, `c_apartment2`, `c_apartment5`,
  `c_apartment3`, `c_apartment4`) in candidate groupings; pick the groupings that pass with the fewest level
  loads left on the chain; record them in `tools/map_groups.json` (wave 1, enabled, hashes from
  `build_group`); a test asserts every enabled group builds to its recorded hashes from the baseline maps.
- [ ] Commit `Map merge: wave 1 groups (apartments)`.

## Phase B: setup and update

### Task B1: merge during setup and update; install record; runtime check

**Files:** Modify `tools/friend_setup.py` (`setup`, `update`, `finish`, `validate_runtime`); Test
`tests/test_friend_setup.py`.

**Interfaces:** Consumes `load_groups`, `install_group`, `GroupInputMismatch`. Produces
`make_merged_maps(root, progress) -> dict[str, str]` (installed files -> sha; skipped groups logged to
`cof-story-merge.log` in the copy); `finish(..., merged: dict)` writes `"merged_maps"` into the installed
record; `validate_runtime` checks every `merged_maps` file like `models`.

Tests: `test_setup_installs_enabled_groups` (fake maps folder with the baseline apartment maps copied),
`test_mismatched_member_skips_group` (one input byte changed: setup succeeds, that group's files absent, log
names it), `test_update_rebuilds_merged_maps`, `test_validate_runtime_rejects_changed_merged_map`.

### Task B2: the app bundles the merger

**Files:** Modify `tools/build_tester_package.py` / the PyInstaller spec it writes if `tools.map_merge` and
`tools/map_groups.json` are not collected automatically; Test `tests/test_build_tester_package.py`
(`map_groups.json` and the `tools.map_merge` modules are in the built bundle listing).

## Phase C: loading a merged map (engine)

Read first: `engine/common/mod_bmodel.c` `Mod_LoadEntities` (2307-2381), `engine/server/sv_game.c`
`SV_ReadEntityScript` (838-919), `engine/common/cof_language.c` (846-906), `engine/common/cof_entpatch.c`.

### Task C1: `.sections` parser (C)

**Files:** Create `engine/common/cof_sections.h` (header-only parser, like `cof_entpatch`), Test
`tests/test_enhanced_sections.py` + `tests/engine/cof_sections_harness.c`.
**Interfaces:** `cof_sections_t` (name, count, per section: map, dz, first_model, model_count, renames,
style pairs, landmarks); `qboolean CoF_SectionsParse(const char *text, cof_sections_t *out)`;
`int CoF_SectionsFind(const cof_sections_t *s, const char *map)`;
`const char *CoF_SectionsRename(const cof_sections_t *s, int section, const char *name)`;
`int CoF_SectionsAt(const cof_sections_t *s, float z)` (section whose height band holds z: bands split
halfway between consecutive offsets' map bounds; the bounds are written by Task A3 as
`bounds <zmin> <zmax>` lines — add to A3's format when this task starts if missing).
Tests: the text written by the Python side for the real group parses to the same values (the test generates
it with `tools.map_merge.rules.to_text`); malformed lines rejected; limits (names <= 63 chars, <= 4
sections, <= 512 renames per section).

### Task C2: entity composition (C)

**Files:** Create `engine/common/cof_compose.h`; Test `tests/test_enhanced_compose.py` +
`tests/engine/cof_compose_harness.c`.
**Interfaces:** `char *CoF_ComposeEntities(const char *worldspawn_lump, const char *const *member_strings,
const cof_sections_t *s, size_t *outlen)` (malloc'd; NULL on a parse error).
Rules (spec section 2): drop each member's `worldspawn`; `origin` z += dz; `model "*n"` -> `*(n - 1 +
first_model)`; values of `REF_KEYS`, of `message` on `path_corner`/`path_track`, and `multi_manager` key
names (with `#n` suffixes) renamed through the member's renames; `style` on `light*` mapped; everything else
byte-for-byte.
Tests: compose the real c_apartment3+4(+5) lumps from the baseline BSPs and compare with the Python
reference composition `tools.map_merge.rules.compose_reference(...)` (add to A3 when this task starts: the
same rules in Python, used only by tests and the planner) — byte-identical output.

### Task C3: wire composition into map loading; `cof_merged_maps`; name resolution

**Files:** Modify `engine/common/mod_bmodel.c` (merged file: read `maps/<name>.sections`, get each member's
string through the existing precedence, compose); `engine/server/sv_game.c`, `sv_init.c`, `sv_cmds.c`
(`map`/`changelevel` resolution), session load; new `engine/server/sv_cof_sections.c`.
**Interfaces:** cvar `cof_merged_maps` (server, default 0; the launcher sets it); `const char
*SV_CoF_MergedFileFor(const char *map, int *section)` (NULL when unmerged or off);
`int SV_CoF_PlayerSection(edict_t *player)`.
Tests: harness for the resolution decision (off -> NULL; member -> file + index; file name itself -> file +
-1; unknown -> NULL); wiring tests (source checks in the existing style) for the call sites.

### Task C4: spawn at the current section's start

**Files:** `engine/server/sv_cof_sections.c`, the spawn path found in `sv_cof_party.c` (`spawn-kept`).
Test: harness for "start spot of section k" selection from the composed entity list.

## Phase D: section moves (engine)

### Task D1: the decision (pure C)

**Files:** `engine/server/sv_cof_sections_move.h`; Test `tests/test_enhanced_section_move.py` + harness.
**Interfaces:** `cof_section_move_t CoF_SectionMoveDecide(const cof_sections_t *s, int from_section,
const char *dest_map, const char *landmark)` -> `{ ok, to_section, src_landmark, dst_landmark }` (renamed
names); fall through (`ok = 0`) when the destination is not a section of this file.
Tests: c_apartment3 -> c_apartment4 via `land1` gives `land1` / `land1_m1`; unmerged destination falls
through; unknown landmark -> `ok = 0, fallback = start`.

### Task D2: movers, offsets, arrival, fade; hooked into `pfnChangeLevel`

**Files:** `engine/server/sv_game.c` (`pfnChangeLevel` first check), `sv_cof_sections.c`,
`sv_cof_party.c` (reuse `CoF_PartyMoveEdict`, arrival placement, the door fade).
Log: `[cof-story-transition] phase=section-move userid=<n> from=<map> to=<map> landmark=<name>
origin=<x,y,z>`.
Tests: harness for mover selection (player in exit + partners within 128 + companions; nobody in exit ->
everyone) and offset maths; wiring test that the check runs before `SV_CoF_ExitRequest`.

### Task D3: exits stay open

Remember (exit models, destination, landmark) of every section move; a later player entering that exit area
is moved the same way. Test: harness "first player through a once-only exit, second player walks in later ->
moved" (Review Focus 3).

### Task D4: `changetarget`

Fire the exit's `changetarget` after a section move (find hl.dll's behaviour by RE first; record it in the
task). Test: harness or wiring test.

## Phase E: per-section behaviour

### Task E1: autosave on first entry into a section
Sections entered kept in the session (`session.dat`); autosave the first time any player enters one. Tests:
harness for "first entry" bookkeeping; session round-trip of the sections-entered set.

### Task E2: sound filter
`ATTN_NONE` sounds from an entity in section X sent only to clients in X; looping sounds of X re-sent to a
player entering X. Test: harness for the per-client decision (Review Focus 5).

### Task E3: companions follow their owner
A section move takes the mover's AI companions. Test: mover selection harness includes companions.

## Phase F: sessions, Continue, launcher

### Task F1: session fields
`session.dat` gains `merged` and `section` (version bump; old saves -> unmerged). Continue keeps the save's
mode. Tests: session round trip; old-version file loads with `merged = 0` (Review Focus 4).

### Task F2: host checkbox and Continue label
`tools/coop_app.py`, `tools/coop_play.py`: "Merged maps (fewer loading screens)" checkbox, on by default,
saved in settings, written as `cof_merged_maps 1/0` into the session cfg for new games and chapters; Continue
shows the save's `section`. Tests in `tests/test_coop_app.py`, `tests/test_coop_play.py`.

### Task F3: run report
`tools/story_run_report.py` counts `phase=section-move` as maps reached and exits proven. Test with synthetic
log lines.

## Phase G: verification and wave 1 release

### Task G1: scripted checks (tools)
`tools/merged_maps_check.py`: the 2026-10-16 method made reusable — camera spots per group, snapshots in the
merged and original maps, pixel comparison report; and a scripted walk into every door between sections with
the `section-move` log line and landing point checked. Unit tests for the pure parts (spot picking, image
comparison, log parsing).

### Task G2: build, package, smoke, notes
Full suite; engine build; package; smoke copy update; AGENT_NOTES; then ask the user for the go-ahead to run
the scripted checks and a live wave-1 playtest.
