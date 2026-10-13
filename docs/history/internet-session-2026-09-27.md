# Remote-friend Internet join test, 2026-09-27

The user requested immediate setup for a remote friend who already has the Steam game installed and Steam open. The host remains the C: development copy; no D: installation changes were made. The joining client can attempt the stock Steam game because only the host's server DLL was patched. A stock remote Steam client subsequently joined successfully, and the user confirmed mutual teammate visibility.

## Configured and verified locally

- Fresh host PID **46016**, started **19:24:36 UTC**, loaded `c_start` at **19:25:12 UTC**. A2S reports one of two slots occupied and a required server password. Authenticated RCON confirms `sv_lan 0`.
- Ethernet address **192.168.1.241**, default gateway **192.168.1.1**. The game UDP socket binds `0.0.0.0:27015`. The earlier `172.17.176.1` printed by `status` was a virtual-adapter advertisement, not its exclusive bind address.
- Both a router WAN query and HTTPS public-address lookup returned **104.33.25.1**. A UPnP mapping now forwards **UDP 27015** to **192.168.1.241:27015**, with a requested **86,400-second lease**. The mapping was read back and its internal client, port, and ownership description matched.
- Host control window PID **49744** retains the mapping handle in the owning Python process. Its **Network & invite → Remove my UPnP mapping** action can remove this lease. Closing the control window leaves the game and lease running; the lease expires after one day.
- Firewall application filters for this exact C: `cof.exe` allow TCP and UDP on the current Public profile. **No firewall rules or profiles were changed.** An initial interpretation of numeric `Action=2` as Block was incorrect; the detailed enum string is Allow.
- `state/remote-session.json` and launcher settings retain the server password. `state/join-instructions.txt` contains the two commands for the remote friend. These private files do not contain the RCON password. Do not copy the broader launcher settings or console logs into an invite.
- A query to the public address from inside the host network timed out. That local hairpin timeout did not establish external reachability. The subsequent successful remote join confirms the Internet route works.

## Faults investigated during setup

Unbound SSDP discovery returned no router replies. Explicitly binding the Ethernet address and `IP_MULTICAST_IF` returned the OpenWrt/MiniUPnPd gateway. `network._discover_gateway` now derives the default-route IPv4 source without transmitting a probe packet, binds that address, and selects it for multicast. Deadline behavior remains bounded. Narrow RCON commands were added for LAN mode and an ASCII server password, retaining command-injection rejection. The full suite passes **66 tests in 9.535 seconds**.

The original process, PID 15908, stopped responding after an in-process server shutdown at 12:21:39 local and menu-map reload. No RCON map change preceded it. User confirmed the application was unresponsive. Main-thread samples establish a wait during `SteamGameServer_Init` → Steam client local-user creation → `WaitForSingleObject`, with a 90-second wait argument. This identifies the blocked subsystem, not the underlying cause or a permanent deadlock. Evidence: `evidence/remote-setup-hang-threads.json` and the redacted console snapshot.

After recording evidence and announcing recovery, only the confirmed unresponsive C: test process was stopped and replaced with the fresh Internet-configured host. The fresh host reached Chapter 1 and answered A2S/RCON; Steam itself and other applications were not restarted. No additional DLL patch was applied.

The first Internet-control helper attempt failed because its `_read_mapping` call omitted the port argument. The helper rolled back its temporary mapping, and a separate read verified that mapping was absent. The corrected helper subsequently opened and verified the current lease. The user-supplied screenshot documents the earlier helper error, not the corrected result.

## Confirmed two-player result

**VERIFIED server evidence:** A2S and authenticated `status` reported **2/2 active players on `c_start`**, including a remote Steam client. **REPORTED by user:** "Yo, it's working, yo. It's working. We can see each other. We're both police officers." This confirms the Internet join, a shared map, and mutual teammate visibility. Both players appear as police officers. Evidence: `evidence/remote-two-player-confirmed.json`.

The active game and router lease remain running. Do not restart or warp the group while they are playing. Two-player map transitions, inventory carry-over, original full-camera traversal, hub persistence, and full campaign progression remain separate unverified areas.

## Campaign blockers reproduced after joining

The successful Internet join does not establish a usable story playthrough. Subsequent live testing found the following concrete blockers:

- **User confirmed:** the remote friend joined with no items. **Static cause established:** `c_start` trigger_once 120 starts `multiscene1`; manager 499 invokes `givemobile` at two seconds and `giveswitchblade` at four seconds. Retail `cof_giveitems::Use` (`hl.dll` preferred VA `0x100A6BC0`) gives to its player activator only. It does not replay for later joins or distribute to the party. The retail game lacks a working `give` console command; `fire` is explicitly ignored. Replaying the whole intro manager would also replay its cameras and teleports, so it is not a targeted recovery.
- **User confirmed:** A later door would not let the remote friend follow, and the host became invisible from the remote friend's view after returning. **Static map evidence:** `c_start2` café manager `multimystery` fires relays that delete both `cafedoors` and `cafedoors2`; another trigger controls the second door's disabled flag. This provides a concrete way for the single-player sequence to strand a trailing player, but the exact door at the reported moment was not captured. The latest validated read-only player snapshot had effects zero and door-in-progress zero for both players, so a persistent EF_NODRAW defect is not established and no visibility patch was applied.
- **Verified logs and user confirmation:** Both clients reached `c_start2` at 12:30 local. Backtracking loaded `c_start` again at 12:35:57. The user reported that Chapter 1's opening cutscene replayed after obtaining the padlock code, then explicitly confirmed returning to the opening scene. The normal-multiplayer transition experiment reloads the destination from scratch and omits landmark/hub restoration. This is now an observed progression defect, not just an untested limitation.

Evidence: `evidence/remote-items-doors-report.json`, `evidence/remote-door-live-player-state.json`, and `evidence/remote-backtracking-replay-confirmed.json`. The game stayed running; diagnostics did not move players, grant broad cheats, replay story triggers, or patch process memory.

The required compatibility work has three distinct responsibilities: restore visited-map state and the appropriate return entrance, synchronize intended story items for joining players, and coordinate the whole party through one-time door/script events. Automatically issuing another plain `changelevel` does not supply those semantics. Preserve these reproductions as acceptance tests for that work.
