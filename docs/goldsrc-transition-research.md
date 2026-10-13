# GoldSrc transition callback investigation

Research date: 2026-09-27. This research changes no game files and starts no game experiment. It includes a read-only inspection of the coordinator's already-running process.

The immediate repair is to correct the binary patch's retained PE relocation, then repeat the original transition with verified loaded instructions. The coordinator found that Windows was relocating bytes which the patch had replaced with instructions: the purported null-landmark patch was malformed after loading. Earlier camera-state, queue, and timing hypotheses are therefore unconfirmed and cannot justify replacing the engine callback. A synthetic automatic transition also succeeded through that callback. A separate probe-revisit failure is now identified as a fatal missing-WAD request caused by omitted probe worldspawn metadata. Ordinary `changelevel` still does not provide single-player state transfer; that limitation is independent of these defects.

## Evidence boundaries

The coordinator supplied these retail Cry of Fear results; they were not independently repeated by this research task:

- Stock `CChangeLevel::ChangeLevelNow` returns early under multiplayer rules.
- Removing that return reaches the engine, which rejects `changelevel2` in multiplayer.
- Also changing the landmark argument to literal `NULL` reaches a natural transition and then the process stops answering A2S and appears unresponsive.
- RCON `changelevel` successfully loads a destination in the same prototype, before the natural trigger is reached.
- The null-landmark test was a **fresh process**, PID 50092, started at 17:26:31. It initially loaded `c_start`, received RCON `changelevel c_nightmare` at 17:27:29, and printed `CHANGE LEVEL: c_start landea` around 17:30 after the camera sequence. The user closed it at 17:32:02; the recorded exit code was zero. There was no earlier rejected DLL transition in that process.
- A synthetic intro brush probe placed the player at the exit and fired a named `trigger_changelevel` through a five-second `multi_manager`. PID 36788 reached `c_start` automatically and answered A2S with one of two player slots occupied. There was no manual RCON exit. Sampling observed loading followed by a live game. This proves that the tested automatic callback path can work; it does not isolate camera state because the probe also changes source map, timing, and touch/use route.
- The coordinator subsequently inspected the loaded patch: a retained `IMAGE_REL_BASED_HIGHLOW` entry at RVA `0x109f07` overlaps the rewritten landmark push. At preferred base `0x10000000`, the disk bytes were `6A 00 90 90 90`; loaded at `0x68ec0000`, they became `6A 00 90 7C E9`. The padding is now executable conditional-branch code. The worker is correcting the relocation entry and adding rebase checks. This is a concrete patch defect, not evidence of a general engine dispatch bug.

The malformed patch invalidates attempts to attribute the earlier differing behavior to camera globals, callback phase, or a broken null API contract. Those remain possible future discriminators only if a correctly loaded patch still fails. A clean process prevents a stale callback latch from a previous rejected request; it does not protect against a malformed relocation table.

Source facts below are independently inspected. ReHLDS is a reconstruction and enhancement of HLDS, not the source of retail Cry of Fear's `hw.dll`. Valve's HLSDK is an authoritative API/game-code reference, not Cry of Fear's private game implementation. Retail disassembly and captured stacks remain the deciding evidence for this binary.

## Concrete patch and asset failures

`IMAGE_REL_BASED_HIGHLOW` adds the load-address delta to a four-byte field. `IMAGE_REL_BASED_ABSOLUTE` makes the loader skip that entry. Replacing an absolute pointer operand with a shorter null push and NOP padding must therefore also neutralize the relocation for that operand; otherwise ASLR changes instruction bytes. Preserve other relocations. Verify both the original expected relocation and the instruction bytes, emulate several load deltas in tests, and compare the patched bytes in a newly loaded process before using a transition result to infer engine behavior. [Microsoft PE base relocation types](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format#base-relocation-types).

For the separate PID 36788 revisit failure, this researcher inspected the existing sampled frame chain and used `ReadProcessMemory` without suspending threads or issuing game commands. `hw.dll` was loaded at `0x04c70000` (preferred file base `0x01d00000`). The fatal-error frame had EBP `0x001595e0`; its local 1024-byte buffer at EBP minus `0x400` read exactly:

```text
WARNING: couldn't open Manhuntforest.wad
```

`dumpbin` establishes the following return-address interpretation. The middle two function names are matched by code/format strings against ReHLDS, rather than obtained from retail debug symbols.

| Retail engine RVA | Meaning |
| --- | --- |
| `0xf2757` | Return address immediately after `MessageBoxA` in the fatal-error routine beginning at `0xf2656`; caption is `Fatal Error`. |
| `0xfbc64` | Return from that fatal-error call when WAD file opening fails; matches `TEX_InitFromWad`, beginning at `0xfbb0b`. |
| `0x62ef8` | Return from the WAD initializer in texture loading; matches `Mod_LoadTextures`, beginning at `0x62d85`. |
| `0x650f9` | Return from texture loading in the brush-model load path. |

The engine's static format at RVA `0x1d0fd8` is `WARNING: couldn't open %s\n`; the message is fatal despite that wording. The saved revisit log shows this happened after RCON `changelevel cof_probe_api`. The inspected baseline tree contains `manhunt.wad`, but no `Manhuntforest.wad`. The source initializes external WADs when `r_wadtextures` is nonzero or a texture lacks embedded pixels. [WAD open and error path](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/textures.cpp#L80-L116), [texture-loading condition](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/model.cpp#L483-L491).

The original probe's worldspawn contains only `classname`, `skyname`, and `MaxRange`; its omitted `wad` key is the concrete map-construction defect. The preceding `c_start` worldspawn includes `Manhuntforest.wad`, while the probe's actual geometry source, `c_intro`, has its own different WAD list. ReHLDS `Mod_LoadEntities` updates global `wadpath` only when it finds a `wad` key; an absent key leaves the previous value intact. This predicts the observed revisit-specific failure exactly. Preserve the geometry source's original worldspawn metadata in the probe, rather than acquiring a WAD named by stale state. The coordinator's revised `cof_probe_api2` generator now does so; successful rerun is a separate validation step. [Worldspawn WAD handling](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/model.cpp#L675-L707). Existing evidence: `evidence/20260927-103718-probe-api/revisit-threads.json`, `qconsole.revisit.redacted.log`, and `revisit.json`.

## Exact source revisions

GitHub's API resolved the branches during this task. Immutable links below avoid changes in `master` and stale line numbers in search caches.

| Source | Commit | Commit date |
| --- | --- | --- |
| [ReHLDS](https://github.com/rehlds/ReHLDS/tree/550f2d62f13f4ebeb029c1d9d1c212133202611d) | `550f2d62f13f4ebeb029c1d9d1c212133202611d` | 2026-09-20 |
| [Valve HLSDK](https://github.com/ValveSoftware/halflife/tree/b1b5cf5892918535619b2937bb927e46cb097ba1) | `b1b5cf5892918535619b2937bb927e46cb097ba1` | 2024-10-02 |

## What the callback actually requests

`CHANGE_LEVEL` is an indirect call through `g_engfuncs.pfnChangeLevel`, whose SDK signature takes two character pointers. It is not a macro expanding to a console command. ReHLDS installs `PF_changelevel_I` in that engine-table position. [HLSDK callback macro](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/enginecallback.h#L33), [API declaration](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/engine/eiface.h#L115), [engine table](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sys_dll.cpp#L102-L124).

The engine callback tests whether the **second pointer is non-null**, not whether its string is nonempty. It accepts only one call per server spawn, records that spawn immediately, calls `SV_SkipUpdates`, and appends the chosen command. Thus a rejected first request consumes the latch. `SERVER_COMMAND` instead validates and appends text; `SERVER_EXECUTE` immediately drains the command buffer. [Callback and latch](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/pr_cmds.cpp#L2464-L2477), [server-command APIs](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/pr_cmds.cpp#L846-L857).

| Argument at actual engine entry | Selected command |
| --- | --- |
| Pointer to `"landea"` | `changelevel2 c_start landea` |
| Non-null pointer to an empty string | `changelevel2 c_start` |
| Literal null pointer | `changelevel c_start` |

The SDK's multiplayer game rules themselves call `CHANGE_LEVEL(szNextMap, NULL)`. This makes null a supported contract, rather than a workaround that the engine inherently cannot handle. [Valve multiplayer map change](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/multiplay_gamerules.cpp#L1600-L1617).

The latch explains why a failed `changelevel2` followed by an in-place DLL fix can appear permanently broken until the next actual spawn. **It does not explain PID 50092 on the supplied chronology.** If a trace unexpectedly finds an earlier accepted callback in that process, reevaluate it; do not promote it to the current diagnosis without that evidence.

## Ordinary change versus single-player transition

Ordinary `Host_Changelevel_f` validates and copies the map token, optionally copies a startspot, starts loading presentation, inactivates clients, deactivates the DLL, spawns a fresh server, loads map entities, and activates it. With two command tokens, `startspot` remains null. The handler does not consult `vecLandmarkOffset`. `Host_Changelevel2_f` explicitly rejects multiple client slots; its successful path saves/restores game state and adjacent entities, then activates with a different mode. Removing only that guard does not make its state model multiplayer-capable. [Ordinary handler](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/host_cmd.cpp#L1063-L1107), [single-player handler](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/host_cmd.cpp#L2393-L2466).

This predicts a functional limitation even after the hang is fixed: a normal multiplayer map change is not the single-player save/restore transfer. Inventory, adjacent-map state, landmark-relative arrival, and delayed transition targets require explicit treatment.

## Where DLL dispatch differs from RCON

The command buffer is a bounded linear byte buffer. Appending copies the bytes immediately. Execution removes a command before dispatch, recognizes newline/semicolon boundaries, and a `wait` yields to the following frame. A full buffer rejects the append; it does not grow forever. The formatter's temporary storage is a separate 16-slot array. Accordingly, the examined implementation supplies no inherent dangling-string or command-buffer-ring explanation. [Command buffer](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/cmd.cpp#L58-L249), [formatter storage](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/common.cpp#L1831-L1844).

RCON dispatches its parsed command directly through `Cmd_ExecuteString(..., src_command)`, bypassing the callback's latch, skip-update operation, and queue. The outer host loop normally drains the command buffer before running the server frame, then checks RCON after it. A trigger reached during physics therefore normally schedules work for a later outer-loop drain. These are real differences worth measuring. [RCON dispatch](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp#L3677-L3692), [host-frame ordering](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/host.cpp#L887-L906).

`SV_SkipUpdates` contains an upstream TODO: its fake-client check dereferences `host_client` rather than the iterated client. The skip flag is cleared on the next send pass, so the flag alone predicts a skipped send, not a permanent hang. Treat an invalid/stale `host_client` as a conditional hypothesis requiring a stack or register observation. [Skip and clear](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp#L5243-L5272).

## Trigger-side state and recursion

Valve's trigger rejects deathmatch, debounces only repeated invocation of the same entity at the same game time, checks player slot 1 against the transition volume, prepares a delayed transition target when present, copies the map into static storage, fires ordinary targets, resolves the landmark, sets `vecLandmarkOffset`, logs, then calls the engine. Its empty-buffer comment uses “NULL” informally: the buffer address passed to the engine is still non-null. [Full trigger path](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/triggers.cpp#L1478-L1545).

Target firing can invoke other entities immediately; the delayed-use path instead creates another entity. This creates a plausible pre-transition difference from RCON. It is not proof of recursion in the Cry of Fear maps. [Target dispatch and delayed use](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/subs.cpp#L189-L284).

Potential discriminators if a relocation-corrected patch still fails; these are not established causes of the earlier runs:

- A target callback may alter game-specific globals or entities that are later used during shutdown. Removing the engine landmark argument does not undo those earlier side effects.
- Nested transitions can overwrite shared map/landmark buffers or let a different request consume the engine latch. Trace callback depth and every requested destination; a single final log line is insufficient.
- Since `CHANGE LEVEL` is logged after ordinary target firing in the reference path, a trace showing that exact sequence lowers the probability of being stuck *inside the preceding target call*. It does not exclude damage caused by that call and used later.
- A stale landmark vector is not enough to explain the engine handler: it does not read that vector. Cry of Fear's shutdown/spawn callbacks might read it, but that requires inspecting those callbacks or capturing their dataflow. Do not clear unrelated globals speculatively.
- A callback that executes the queued command synchronously could replace/free the current world's entities while old touch/think frames are still on the stack. A trace must distinguish ordinary deferred dispatch from reentrant dispatch. Do not add `SERVER_EXECUTE()` inside the trigger as an untested “flush” fix.

## Separate server load from client reconnection

The reference engine calls game-DLL `ServerDeactivate` during shutdown and `ClientDisconnect` during spawning. Those are useful boundaries before entity loading. Activation runs entity/game setup and settling physics, then sends multiplayer reconnect messages and writes the started-map log. [Shutdown and spawn callbacks](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp#L6357-L6404), [activation and reconnect](https://github.com/rehlds/ReHLDS/blob/550f2d62f13f4ebeb029c1d9d1c212133202611d/rehlds/engine/sv_main.cpp#L6247-L6343).

The standard SDK's `ServerDeactivate` contains an activation guard and otherwise no substantive shutdown work; `ServerActivate` traverses eligible entities and invokes `Activate()`. Cry of Fear can have materially different implementations. [Valve callbacks](https://github.com/ValveSoftware/halflife/blob/b1b5cf5892918535619b2937bb927e46cb097ba1/dlls/client.cpp#L690-L735).

A new-map A2S reply while a player's screen is stuck identifies a different stage from a main-thread loop with no A2S response. A started-map log is useful but still does not prove that every real client has reconnected or that inventory was restored.

## Upstream reports: discriminators, not diagnoses

| Report | What its evidence supports | Relevance to this run |
| --- | --- | --- |
| [Valve #1206](https://github.com/ValveSoftware/halflife/issues/1206) and [#1200](https://github.com/ValveSoftware/halflife/issues/1200) | Captured hangs inside entity parsing during `SV_LoadEntities`; discussion isolates malformed/high-byte entity text in affected maps. Valve requested a stack from the hung process. | Only pursue if the stack is in parser/loading code. Loading the same unmodified destination successfully through another route reduces the chance of a deterministic destination-only parse failure. |
| [ReHLDS #960](https://github.com/rehlds/ReHLDS/issues/960) | A map-change crash stack reaches model cache allocation during activation, after command dispatch. | Demonstrates why the last console command alone cannot locate failure. It does not establish cache corruption in Cry of Fear. |
| [Valve #3641](https://github.com/ValveSoftware/halflife/issues/3641) | A multiplayer soft-restart/renderer-change reproducer and Steam-client mutex stack. | Low priority without `_restart`, renderer change, or a matching Steam wait stack. Ordinary map change is not the reported reproducer. |
| [ReHLDS #226](https://github.com/rehlds/ReHLDS/issues/226) | An old five-minute freeze report, with no useful confirming stack or resolution. | Insufficient basis to blame storage, plugins, or a particular engine bug. |

## Conditional follow-up after repairing the patch

First verify loaded patch bytes and preserve the probe's source worldspawn metadata. Repeat the previously failing original transition in a fresh process. If it succeeds, do not add callback instrumentation or a watchdog to solve an absent defect. If it still fails, use the coordinator's isolated probe and sampler. Record map generation, frame/time, thread ID, destination, null/non-null pointer status, callback nesting depth, and stage entry/return. Retain relevant map command tokens rather than indiscriminately recording command buffers containing settings/passwords.

Suggested stages:

1. Trigger handler entry; before/after target dispatch; before/after engine API.
2. `PF_changelevel_I` entry/return, latch comparison, before/after skip-updates, append accepted/rejected.
3. Next outer command-buffer dispatch and actual tokens.
4. `Host_Changelevel_f` entry, loading plaque/audio boundary, inactivate, DLL deactivate, each disconnect callback.
5. Server spawn, entity loading, DLL activate, settling physics, reconnect emission, and handler return.

| Result | Next focused test |
| --- | --- |
| Engine API never returns | Sample inside skip-updates/formatting/queue operation and inspect its actual arguments. |
| API returns but command never dispatches | Check current main-thread stack, queue length, overflow, repeated `wait`, and who clears/modifies the queue. |
| Dispatch enters ordinary handler and then stalls | Fix the first stage that does not return; changing the console command string cannot fix a later DLL callback loop. |
| Handler returns and server answers on destination | Trace client reconnect and game-specific player restoration separately. |
| Minimal synthetic trigger works; original trigger fails with verified code | Compare direct touch versus delayed use on the same probe, then compare the same source/destination map pair and target graph. Only then isolate camera/phone state and callback depth. |
| Direct API fails but plain queued `SERVER_COMMAND` succeeds at the same boundary | The API's additional side effects/latch become strong candidates. This is narrower than comparing a much earlier RCON warp. |
| Both queued paths fail but direct command dispatch succeeds | Inspect queue execution context or a reentrant command drain. Do not immediately execute from touch/think to conceal it. |

Keep each comparison in a new server generation and use the same destination/content. Preserve the original result and stack when changing one factor.

## Recommended fix direction

The first implementation candidate is the **relocation-aware correction to the existing patch**, followed by a fresh-process original-trigger test and a probe preserving source worldspawn metadata. Keep the supported null-landmark engine API route. Current evidence provides no reason to substitute a custom command queue or a lossy RCON watchdog.

Only if a corrected binary still fails should a transition API shim be considered, initially for diagnostics and reliable event capture. Copy map and landmark into owned storage, record server generation and request identity, and preserve the normal return/deferred execution behavior. If a trace proves a defect in the callback's extra work, test validated `SERVER_COMMAND` at the same safe boundary without `SERVER_EXECUTE`. If the original API returns and a later shutdown/load stage fails, repair that stage. If immediate execution or target recursion is actually observed, correct that specific path. Completion belongs to a confirmed activation/server generation change, not an elapsed-time threshold; a permanent block on revisiting maps would break campaign hubs.

A thin engine-function proxy avoids interpreting standard `entvars_t`/`edict_t` layouts just to capture the request. Still verify the actual Windows calling convention, table layout, loaded-module identity, and engine/GameDLL ABI before installing it. A stock Metamod/SDK structure layout is not automatically compatible with this game.

For a state-preserving campaign fix, the same captured event must drive explicit transition state: stable player identity and inventory, source/destination landmark transforms, shared quest state, delayed transition targets, and per-map hub snapshots. Restore only after destination entities and clients are ready. That work belongs in a compatible game/engine source layer or carefully verified game API hooks. Simply enabling multiplayer in `changelevel2`, passing an empty string, forcing immediate `SERVER_EXECUTE`, or polling a log and issuing RCON is not evidence of preserving that state.

The supported null callback is consistent with the source and the successful automatic probe. **Repair the malformed relocation and the probe's omitted worldspawn metadata before interpreting further transition behavior.** The earlier general timing/engine-fault hypotheses are unsupported by the available evidence; state-preserving campaign transitions remain separate work.
