# Merging the rest of the campaign: what merges, what blocks it, and the order to do it in

Date: 2026-10-17. The user: "read the map merge research that was done and start figuring out how we are gonna
merge the rest". Builds on the merged-maps design (`docs/superpowers/specs/2026-10-16-merged-maps-design.md`),
the spike (`evidence/map-merge-spike-20261016/`) and wave 1 (the apartments, live since 2026-10-16).

Evidence (git-ignored): `evidence/map-merge-plan-20261017/`: every door-joined story pair and candidate group run
through the real planner (`tools/map_merge/plan.py`: it merges for real and checks every format, renderer, engine
and game DLL limit), and `hl-names/hl-map-names-re.md`, a static reverse-engineering report of what hl.dll (and
client.dll) do with the map names the planner refuses.

## The short answer

- The story has 81 maps and 88 door-joined pairs. Today the planner accepts **13** pairs (wave 1 uses three of them).
- **Small merger rule changes** (section 2) bring that to **39 pairs** (24 by the worldspawn rules, 7 more by the
  member order, 8 more by lifting the hl.dll name refusal) and these groups (no map in two groups):

  | Group (first member stays, the others stack above or below) | Lightmap pages | Doors removed |
  |---|---|---|
  | c_sewer3 + c_sewer2 | 77 | 2 |
  | c_city2 + c_park | 75 | 2 |
  | c_hamn + c_hamn2 + c_hamn3 | 83 | 4 |
  | c_college2 + c_college1 + c_college1dark | 79 | 4 |
  | c_buscity + c_buscity2 | 69 | 2 |
  | c_subway2st3 + c_subway2st2 | 80 | 2 |
  | c_forest2 + c_forest3, c_forest4 + c_forest5, c_forest6 + c_forest7 | 64, 44, 52 | 6 |
  | c_asylum1dark + c_asylum2dark + c_asylumb + c_asylum2 | 88 | 6 |
  | c_asylum1day + c_attic + c_forestday | 96 | 4 |
  | c_fsewer1 + c_lake | 78 | 2 |
  | c_village1 + c_village2 + c_village3 | 137 | 4 |
  | c_ending4b + c_ending4 (file `cm_c_ending4b`, rule 2.3) | 54 | 2 |

  "Doors removed" counts level-door directions inside the group; a playthrough crosses most of them once or
  twice, the hubs more. This is the wave 2 list the user chose on 2026-10-17 (section 6 says why each overlap
  went the way it did).
- **Four pieces of engine and merger work** unlock most of the rest, in this order of value:
  1. **Per-section fog events**: Chapter 1's c_start + c_start2 and nine more pairs.
  2. **Origin-brush models in a moved member** (mostly `func_train`): the subway, subtunnel and train chains.
  3. **Play-everywhere sounds that start with the map**, sent per section: the asylum1 and train pairs.
  4. **Larger coordinates** ("big world"): 4-member stacks, and maps too tall to stack today.
- **Stays separate:** anything with c_nightmare (client.dll checks the name), the c_city hub, the ending b/c
  pairs and c_subwaysick1/2 (too tall or too many clipnodes to stack), and the five clipnode-overflow pairs. They
  remain ordinary level loads.

## 1. Method

`survey_pairs.py` runs the planner on every pair of story maps joined by a level door (`pairs.json`,
`story_pairs.txt`); `after_rules.py` counts the worldspawn differences section 2 settles as solved
(`after_rules.txt`); `reverse_origin.py` plans the origin-brush pairs the other way round (`reverse_origin.txt`);
`hl_pairs.py` plans the pairs refused only for an hl.dll name with that refusal lifted (`hl_pairs.txt`);
`plan_groups.py` plans 3-4 member chains (`groups.txt`). About 4 s a pair.

Blockers over the 88 story pairs, as the planner reports them today:

| Pairs | Blocker | Kind of work |
|---|---|---|
| 27 | a map hl.dll names | mostly nothing (section 3) |
| 27 | worldspawn keys differ | merger rules (section 2) |
| 21 | an origin-brush model in a moved member | 7 pairs: put that member first; the rest: merger work (4.2) |
| 11 | fog differs | engine work (4.1) |
| 7 | a play-everywhere sound starts with the map | engine work (4.3) |
| 6 | a key names a renamed entity but is not renamed | merger rules (4.5) |
| 5 | too many clipnodes | inherent: stays a load |
| 3 | sky differs | engine and client work (4.6) |

## 2. Merger rule changes (no engine work)

1. **worldspawn keys hl.dll reads.** `player1`-`player4` and `materialstxt` differ in 18 pairs; wave 1 already
   accepted them (the engine sets player models itself; the default materials file is the same file). Make that a
   rule instead of a per-group exception. `MaxRange` (view distance) differs in 15 pairs: the merged worldspawn takes
   the largest. `WaveHeight` missing equals 0.0 (c_start / c_start2).
2. **Member order.** The first member is not moved, so its origin-brush models are fine: the planner should try the
   member with origin brushes first (this alone unlocks 7 pairs).
3. **hl.dll names** (section 3): lift the refusal for c_city, c_forest3, c_intro, c_start, c_subwaysick1 and the
   c_ending maps; keep c_nightmare and c_game_menu1. A group containing a credits map (c_ending1b/2b/3b/4,
   c_ending5b) needs a file name that contains `c_ending` (e.g. `cm_c_ending4b`), because hl.dll's credits test is
   `strstr(mapname, "c_ending")`; `groups.py` today forces `cm_` + the first member without `c_`.
4. **The c_start easter egg.** hl.dll's worldspawn spawns one `dave_benson_phillips` at (613, -3156, -702) when the
   map is exactly `c_start`. A merged c_start group adds that entity to the c_start section itself.

Each of these is small (planner and composer, with tests) and changes nothing for wave 1's files.

**Done for wave 2 (2026-10-17):** rule 1 (`plan.WORLD_KEYS_IGNORED`, `plan.WORLD_KEY_DEFAULTS`, the largest
`MaxRange` in `merge.merged_worldspawn`); rule 2 by hand (the group list puts the member with origin brushes
first, e.g. c_college2's train); rule 3 (`groups.NEVER_MERGED` is c_nightmare, c_game_menu1 and c_start;
`groups.group_name` keeps `c_ending`). c_start stays refused until rule 4 (wave 3 needs it with the fog work anyway).
The planner also checks what the engine's `.sections` reader holds (512 renames, 32 styles, 64 landmarks a section,
names up to 63 characters: `cof_sections.h`), and `python -m tools.map_merge.plan MAPS --record WAVE m1 m2 ...` plans,
builds and writes a group into `tools/map_groups.json` with its hashes.

## 3. What hl.dll does with the map names (summary of `hl-names/hl-map-names-re.md`)

| Name | What depends on it | If merged |
|---|---|---|
| c_city, c_forest3, c_subwaysick1 | only `map <name>` strings in an unreferenced chapter-start function | nothing |
| c_intro | the single-player New Game think loads it | nothing (co-op never runs it) |
| c_start | worldspawn spawns the "Dave Benson Phillips" easter egg | the egg is missing unless added (rule 2.4) |
| c_nightmare | `cof_begingame` sets a player byte (server: harmless); **client.dll** compares `maps/c_nightmare.bsp` in 4 places (hides HUD panels, no sprint, no inventory or objectives) | the nightmare would show the HUD and allow sprint/inventory: **keep unmerged** (or teach the client the section's original name, later) |
| c_ending* (substring) | `cof_credits` evaluates the ending, the rank and the unlocks only when the map name contains `c_ending` | a group with a credits map must keep `c_ending` in its name (rule 2.3) |
| c_game_menu1 | the menu map | not a story pair |

Also seen: client.dll lights some models differently in maps whose name contains `college1dark` or
`forest1`-`forest6` (probably cosmetic; check in the pictures run of those groups). Showing a section's original
name in `gpGlobals->mapname` during callbacks is not a good general fix: hl.dll also builds the node graph file name
and the team-wipe restart from it.

## 4. Engine and merger work, in order of value

### 4.1 Per-section fog events (unlocks Chapter 1's c_start + c_start2, and 9 more pairs)

Every `env_fog` in the story is triggered by the script (a targetname); none is on from the map start. A pair is
refused when its members' fog events differ (wave 1's two apartments have identical ones). In a merged map one
member's fog event would fog the players of every section, and a fog seen in one section would travel with a player
through a section move (a level change resets it, a section move does not). Work: find how hl.dll's `env_fog` sends
its fog (static RE: a user message to all, or to the activator), send it only to the players of the entity's
section, keep each section's current fog, and send the destination section's fog (or none) with every section move.
Then: c_start + c_start2 (with rules 2.1 and 2.4), c_basement + c_sewer, c_park + c_park2, c_park2 + c_stad,
c_park3 + c_roofs2, c_roofs + c_roofs2, c_apartment3sick + c_apartment4sick / c_down / c_avlopp, c_ending1b-3b with
c_ending1-3, and c_subway2st3 + c_trainscene (which also needs 4.2 and 4.6). Check wave 1 too: cm_apartment1's two fog events reach both sections today.

### 4.2 Origin-brush models in a moved member (the subway, subtunnel and train chains)

Brush models built around an origin (`func_train` in 13 story maps, plus 4 `func_door_rotating`, a `func_rotating`,
a `func_valve`, c_sewer's `func_fogfield`s) keep their geometry relative to the entity's origin. The merger moves a
member's vertices, planes and texture alignment, and the composer moves every origin: such a model would move twice.
Work in the merger: leave those models' planes, vertices, texinfo and bounds unmoved (giving them their own copies
of anything they share with moved geometry) and extend `verify.py`. About a day. Unlocks c_buscity + c_subway2st1,
c_subway2st1 + c_subtunnel2, c_subtunnel1 + c_subway1st1, c_stad + c_subway1st1, c_subway2st1 + c_subwaysick2, the
trainride pairs (with 4.3) and 3-member chains around them.

### 4.3 Play-everywhere sounds that start with the map

An `ambient_generic` that plays everywhere (spawnflags 1) and starts with the map goes into the sign-on data, which
every player gets: every section would hear it. Work: send such a sound only to its section's players, when they
first enter the section (the per-section sound filter covers sounds started later already). Unlocks c_asylum1 +
c_asylum2 / c_attic / c_forest7, c_bridge + c_broscene, the trainride pairs (with 4.2), c_ending5 pairs.

### 4.4 Larger coordinates ("big world")

Entity origins go to clients as 16 bits of eighths of a unit (`delta.lst`), so ±4096: four members rarely fit
stacked, and tall maps (c_city, c_ending1c-3c) do not stack at all. Xash can send larger coordinates (a modified
`delta.lst` plus large-coordinate messages), but client.dll reads coordinates in its own user messages with its own
`READ_COORD`. Work: research first (which client.dll messages carry coordinates). Unlocks 4-member chains and the
c_city hub (c_city + c_city2 / c_sewer3 / c_buscity / c_avlopp).

### 4.5 Keys that name a renamed entity (6 pairs)

The planner lists keys whose value is a renamed targetname but that the composer does not rename (for example a
`func_door` message or a CoF-specific key). Work: per key, decide whether it refers to an entity and add it to the
rename keys (`rules.REF_KEYS`), with a test. Small.

### 4.6 Sky per section (3 pairs: c_subway2st1/2st2/2st3, c_trainscene)

A merged map has one sky. Work: switch the client's sky on a section move (engine-side `sv_skyname` change and a
client reload of the sky). Research first.

### 4.7 c_nightmare (later, low value)

Teach the engine's client side to report the section's original map name to client.dll's 4 checks of
`maps/c_nightmare.bsp`. Only the intro and its exit benefit.

## 5. Proposed waves

- **Wave 2: merger rules only** (section 2), the groups in the first table, each with the automated live checks
  (`tools/live_loop.py`: every door between sections with two AI companions; `tools/merged_maps_check.py`:
  door landings and pictures merged vs original) and then a playtest per chapter.
- **Wave 3: per-section fog** (4.1): Chapter 1 (c_start + c_start2), the park, roofs, basement-sewer and sick
  apartment groups; recheck cm_apartment1.
- **Wave 4: origin brushes and start-up sounds** (4.2, 4.3): the subway, subtunnel, train and asylum1 groups.
- **Wave 5: research items** (4.4 coordinates, 4.6 sky, 4.7 c_nightmare), each only if the playtests show the
  remaining loads are worth it.

## 6. The overlaps, decided (2026-10-17)

The choices were: c_sewer + c_sewer2 or c_sewer3 + c_sewer2; c_stad + c_hamn + c_hamn2 or c_hamn2 + c_hamn3 +
c_college1; the dark asylum group or c_asylum2 + c_asylumb; c_buscity + c_buscity2 or c_subway2st1 + c_buscity
(after 4.2); c_village3 with one ending. The rule used: the most loads removed on the route actually walked, and on
a tie the choice that leaves a map free for a later pair. More groups were planned for this
(`evidence/map-merge-plan-20261017/overlaps.txt`, `more_groups.py`). The user agreed ("Do it").

- **Sewers: c_sewer3 + c_sewer2.** Either pair removes one load on the walk from chapter 2 into 3; this one leaves
  c_sewer for c_basement after 4.1 (`c_sewer + c_basement` fails only on fog), so that stretch ends with one load
  instead of two. All three sewers: too many clipnodes.
- **Harbour and college: c_hamn + c_hamn2 + c_hamn3 and c_college2 + c_college1 + c_college1dark** (neither listed
  option). The college connects only to c_hamn3 and the harbour only to c_stad, so the branch is walked in and back
  out: 10 loads from the stadium round trip. c_hamn3's door to the college switches to c_college1dark once the
  player is back (`exdoorch1`). These groups remove 6, as many as the best listed option, and keep c_stad free for
  `c_stad + c_park2` after 4.1 (only fog left). `c_hamn3 + c_college1 + c_college1dark`: too many vertexes; all
  four with c_hamn3: too tall.
- **Asylum: c_asylum1dark + c_asylum2dark + c_asylumb + c_asylum2.** One pass: c_forest7 → c_asylum1dark →
  c_asylum2dark → c_asylumb, whose power puzzle switches the basement door from c_asylum2dark to c_asylum2
  (`changeexio`) → c_asylum2 → c_asylum1 → c_attic. Three loads removed, against two or one. c_forest7 stays with
  c_forest6 (joining it to the asylum instead removes one load fewer overall).
- **Bus city: c_buscity + c_buscity2.** It needs no engine work; c_subway2st1 is the subway hub, with other partners
  after 4.2 (c_subtunnel2, c_subwaysick2).
- **c_village3 stays in the village group:** two loads every playthrough against one load in one ending.
- client.dll draws one model type nearer in maps named `college1dark` or `forest1`-`forest6` (section 3): in
  `cm_forest6` the c_forest7 section gets that, in `cm_college2` the c_college1dark section loses it. Cosmetic;
  look for it in the pictures run.
