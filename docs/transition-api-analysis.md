# Retail transition API diagnosis, 2026-09-27

The actual retail engine's `pfnChangeLevel` callback queues a console command. It does not immediately load a map or recursively invoke the changelevel handler. Replacing the landmark pointer with null selects the ordinary queued `changelevel` command. The observed hang therefore needs a captured stack and command-path trace; an immediate/reentrant callback explanation is contradicted by this binary.

No game or DLL modifications were made during this analysis. All addresses below are preferred-image addresses, not guaranteed runtime addresses. Add the stated RVA to the debugger's actual module base.

## Examined binaries and method

| File | SHA-256 | Preferred base |
| --- | --- | --- |
| `baseline/Cry of Fear/hw.dll` | `9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875` | `0x01D00000` |
| `baseline/Cry of Fear/cryoffear/cl_dlls/hl.dll.orig` | `0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63` | `0x10000000` |

**STATIC:** Python `pefile` resolved file offsets, function tables, strings, and field metadata. Installed Microsoft `dumpbin /disasm /range` inspected bounded instruction ranges. ReHLDS source was used to corroborate names after matching the local machine code, not as a substitute for inspecting this retail binary.

## The callback table is directly identified

1. `hl!GiveFnptrsToDll` at VA `0x100B4AA0` copies `0x90` DWORDs from the supplied table to `0x10222F48`, then stores the supplied globals pointer at `0x10223188`.
2. The level-change call in `hl!ChangeLevelNow`, VA `0x10109F10`, uses `[0x10222F60]`. The offset from the table base is `0x18`, or **slot 6**.
3. In `hw.dll`, the loader obtains `GiveFnptrsToDll`, then at `0x01DF3999–0x01DF39A3` passes globals `0x0251F3C0` and function table `0x01ECED18`.
4. That table's slot 6 contains `0x01DA6BC8`. This identifies the actual retail callback, RVA **`0xA6BC8`**.

## What the retail callback does

**STATIC, complete callback range `0x01DA6BC8–0x01DA6C2C`:**

- Compares `[0x0251F330]` with `[0x0242F0F0]`. If equal, it returns.
- Otherwise copies the current value into the latter variable and calls `0x01DDE78A`.
- Tests the landmark pointer at `[ebp+0x0C]`.
- If null, formats `changelevel %s\n` using the map argument and calls `0x01D440EB`.
- If non-null, formats `changelevel2 %s %s\n` using map and landmark and calls the same function.
- Returns immediately after adding that text. There is no call to command execution or map loading here.

The strings were inspected as bytes; both include a real newline. `0x01D440EB` is the command-buffer append routine: it checks available buffer space, references the literal `Cbuf_AddText: overflow`, writes the string bytes, and returns. It does not execute them.

The latch corresponds to current `spawncount` versus static `last_spawncount`. It admits one request per server/map generation. A rejected `changelevel2` can leave this latch consumed while the same map remains running, so repeatedly touching the exit will not retry through this callback until the generation changes. Conversely, the latch prevents unlimited requests from repeated touches in the same map. This structure matches [ReHLDS's PF_changelevel_I](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/pr_cmds.cpp#L2424).

There is no infinite loop in this callback. A later command handler, game callback, render/client path, or synchronization operation could still hang; that must be located independently.

## The precondition helper skips one client update

**STATIC:** Helper VA `0x01DDE78A`, RVA `0xDE78A`, is a finite client-array loop:

- Iterates `maxclients` at `[0x0251F328]`, from the client array at `[0x0251F324]`, with stride `0x5018`.
- Checks whether each client has any of its active/connected/spawned fields set.
- Tests `[host_client + 0x2548]` using global host-client pointer `[0x027FC11C]`.
- Sets the iterated client's field `+0x3594` to 1.

In the send loop beginning at `0x01DDE7FF`, the engine checks that same `+0x3594` field at `0x01DDE883`, clears it at `0x01DDE891`, and skips that iteration. The adjacent `+0x3590` field controls sending. These match `SV_SkipUpdates`, `fakeclient`, `skip_message`, and `send_message` in [ReHLDS's corresponding source](https://github.com/rehlds/ReHLDS/blob/master/rehlds/engine/sv_main.cpp#L5175).

The unusual check of **global host_client**, rather than the client being iterated, is present in the actual retail code and is also called out in that source. It can misclassify clients, but it is not itself a loop and does not establish the hang's cause. Both landmark branches perform this helper before queuing. An ordinary RCON `changelevel` bypasses this callback's latch and skip-update preparation.

## Plain console changelevel path

**STATIC:** Command registration at `0x01D8655B–0x01D86565` binds `changelevel` to VA `0x01D81C67` (RVA `0x81C67`). Adjacent registration binds `changelevel2` to `0x01D84F8F`.

The ordinary handler validates arguments, server state, and map existence. It copies the map name, obtains an optional second argument, then calls:

```text
0x01D81D51 -> 0x01DE2927
0x01D81D56 -> 0x01DE04A3
0x01D81D65 -> 0x01DE04D1 (arguments: 0, map, optional start/landmark text)
0x01D81D6D -> 0x01DE0DDB
0x01D81D74 -> 0x01DE007A (argument: 1)
```

These are recorded as addresses rather than asserting names for every internal function. The callback's null-landmark command contains only the map argument, so this handler takes its two-argument command case and supplies a null optional string. Manual `changelevel c_nightmare` reaches the same registered command handler with the same textual arguments; the difference is preceding state and execution timing.

## Game-DLL side effects before the engine callback

**STATIC, `ChangeLevelNow`, VA `0x10109D40`:**

1. The original multiplayer rejection is at `0x10109D60`. The allow-transition patch removes it.
2. It compares current game time with an entity timestamp and writes that timestamp. This debounces repeated calls within a frame; it is not a deferred task or loop.
3. It selects entity index 1, then checks transition-volume membership. `c_intro` has no `trigger_transition` volumes; the helper defaults to acceptance when none match.
4. For a player object, it writes `1337` to player offset `0x4C8` at `0x10109DF1`. The player's typed save-field table identifies this member as **`m_iPlayerSaveLock`**. Stock code clears it at `0x100F1974` in the restore path and at `0x100EB50D` in a spawn/load path. The examined direct instruction references are these writes; no direct polling loop on this field was found. Serialization uses the field metadata, so the absence of direct reads is not proof it has no effect.
5. It optionally creates a delayed target-firing helper if the transition's additional change target is set. The intro exit has no such key.
6. It copies the destination map into global buffer `0x10231014`, stores the activator as an entity handle, and invokes its normal targets. The intro exit has no regular target or killtarget; these are distinct from its `map` and `landmark` keys.
7. It clears and, if found, fills landmark buffer `0x10231034`; it also copies the landmark entity origin into the globals landmark-offset vector.
8. It emits the DLL alert, then passes the two global string pointers to slot 6. The plain patch changes only the pushed landmark argument to null; it leaves all preceding single-player-oriented side effects intact.

The save lock and landmark state are concrete differences from a manual RCON command. Their involvement in the hang is **unproven**. Do not remove more instructions merely because they precede the callback.

## Fast probe and debugger discrimination

**PROPOSED, not executed by this investigation:** A disposable probe BSP can be generated from the user's own `c_intro` geometry while replacing only its entity lump. Keep worldspawn, the existing exit brush `*141`, its `c_nightmare` destination and landmark, and a player start next to the exit. Remove the intro's cameras, managers, trains, and audio. Enter the exit through a short movement or deterministic delayed relocation after the server starts. This exercises the real `trigger_changelevel` path without a three-minute cutscene. Keep the original map untouched and do not distribute the derived game geometry.

Give the probe a separate map name and record that caveat: the game may special-case `c_intro`, and removing entity load changes the test. Preserve a second comparison against the actual intro only after the small probe establishes where the failure occurs. Test the same probe with stock, gate-only, and null-landmark variants in separate runs; a consumed callback latch otherwise confounds repeated attempts in the same map.

Useful conditional breakpoints/logpoints, using actual module bases:

| Point | Module + RVA | Question |
| --- | --- | --- |
| Game transition entry | `hl + 0x109D40` | Which entity/activator enters, how many times, and with which patch? |
| Game's engine callback call | `hl + 0x109F10` | Are map and landmark arguments as intended? |
| Retail callback entry | `hw + 0xA6BC8` | Is the generation latch equal, and does the null branch run? |
| Packet-skip helper | `hw + 0xDE78A` | Is host_client valid and how many clients are visited? |
| Command-buffer append | `hw + 0x440EB` | Was exactly `changelevel c_nightmare\n` appended? |
| Ordinary changelevel handler | `hw + 0x81C67` | Does the queued command execute, and what state differs from RCON? |
| Main map setup call | `hw + 0xE04D1` | Does map setup return? |

If the process hangs, capture every thread's stack and the current instruction pointer before modifying anything. A breakpoint hit at command append but not at the ordinary handler narrows the issue to the remaining frame/command dispatch. A hit in the ordinary handler that does not return points toward map teardown/setup or later game callbacks. A responsive server with an unresponsive client instead points toward client/UI state. These outcomes separate hypotheses without treating a black screen as an engine-thread deadlock.
