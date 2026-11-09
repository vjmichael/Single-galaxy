# PLAY-STATION-02 — Persistent station spawning and owned-ship boarding

## Physical authority
A real player-person can spawn **at a station** in original C++ Simulation, rather than implicitly aboard a ship. `PlayerPersonState::AtStation=4` and `physical_station/station_x_mm/station_z_mm` are serialized in backward-compatible v122 original kernel snapshots. AtStation has **no physical ship**. Ordinary game genesis and the intended one-colony-ship initial scenario are unchanged.

The explicit station-test fixture creates one docked owned ship ONLY with a brand-new test save; it never recreates a destroyed hull on restart. Original read-only `station_launch::available_ship` checks a genuinely existing, owned, intact, ready, docked ship at the same station; no shipyard assets appear for free.

The original `station_player_action` supports WALK (bounded 100 mm step in +/-8 m test room), BOARD and DISEMBARK. Boarding requires proximity to the station portal (within +/-600 mm), actual vessel owner, intact hull, berth station, docked/jump-ready state and finite colony crew when applicable. Disembarking requires an owned ship physically docked at the current station. Wrong owner, remote ship, missing ship, unknown station, player at distance, duplicate receipt and standing in space are denied. Station residents may not remotely thrust, dock/undock or mine.

The `PersistentWorld` wrapper mutates a copy of original Simulation inside the canonical original STEP and immutable snapshot/journal SQLite writer transaction and inserts a unique `sg_station_player_action` receipt under that SAME lock. Fault injection proves transaction rollback. Full independent tamper-chain proofs remain open.

## Native process and Godot connection
New first-party C++ `galaxy_desktop_station_host SQLITE_PATH --explicit-test-station` is a test-only child process with fixed local owner, bounded request/rate and authoritative session/generation checks. It receives `WALK|session|req|generation|dx|dz`, `BOARD|session|req|generation|ship`, or `EXIT|session|req|generation|station`, and returns `PERSON|2|session|req|tick|generation|person_state|station|ship|x|z|eligible_owned_ship|receipt` or `DENY|2|req|reason`. Godot never chooses person ID or writes direct SQLite.

In the installed Godot editor project, with compatible locally compiled Windows native hosts:
- Press **H** for isolated station save `single_galaxy_station_test.sqlite`; WASD moves player only from original native ACK.
- Approach the marked floor hatch near native (0,0), press **E** to board the currently available owned ship. No ship = visible refusal, no free hull.
- Press **P** to close station writer and resume original cockpit host ON THE SAME saved SQLite world; P is blocked before boarding acknowledgement.
- **E** sits in real cockpit, **U** undocks, existing PR #113 A/D, W/S, Up/Down and B control original ship.
- After actual return/dock, press P to close cockpit, H to reopen station test writer, E to disembark. F7/F8 are Godot editor shortcuts, not game actions.
An already-installed Godot editor is sufficient; `BUILD_WINDOWS.cmd` only packages a standalone export.

The visible station marker and prototype ship room do not yet match physical ship mesh/berth locations; this is real native custody and ACK-directed avatar coordinates, NOT finished visual boarding animation or installed UI acceptance.

## Tests actually run on Linux
Exact source native Release build linked original kernel, station host, original cockpit host and test executables. CTest **8/8**: station lifecycle; interior authority; FP3D02F pipe; FP3D03B cockpit original physics and native pipe; PLAY-STATION-01 1-tonne resource loop; new original station boarding/dock/disembark/SQLite and no-free-ship fault tests; new real station child process -> walk -> BOARD -> restart -> real original cockpit child -> sit -> UND -> deny exit in space.
Python Godot source invariants and Windows-builder source checks also passed. A synthetic Godot station parser smoke and native Windows PowerShell process handshake are registered, but have not been executed on Godot/Windows.

## Native writer exclusivity and handoff repair (November 8)

- Both original desktop C++ writer hosts now acquire a **process-lifetime OS lease** against `<save>.sg-native-writer.lock` before opening their SQLite world. Linux uses nonblocking `flock`; Windows uses an exclusive `CreateFileW` handle. A second station/cockpit native host opening the same save must fail *before* it can emit an authoritative frame. The intentionally persistent sidecar is never unlinked during operation, preventing POSIX inode-lock circumvention; the kernel automatically releases custody on process death.
- Godot H/P transitions now queue a target and require `OS.is_process_running(previous_writer_pid) == false` before starting the other native host. Recent stopped PIDs are retained for three seconds so rapid repeated key presses still wait. If an old writer remains alive after three seconds, the transition aborts, **not** a second launch. Local unauthoritative avatar walking is suppressed while awaiting handoff; a fresh verified native station PERSON frame is required before opening the cockpit.
- Native real-child pipe tests now intentionally start both *a second station writer* and *a cockpit writer* while the first process holds the original station save and verify both are rejected, then continue the real WALK→BOARD→EXIT/restart→cockpit sequence. The Windows PowerShell acceptance source includes the same competing-writer check. Native Linux build and eight selected CTests passed after these changes; Godot 4.7.2 engine runtime and physical Windows acceptance remain unverified.

## Remaining gates
Actual Windows C++ compilation + native station/cockpit host execution, Godot 4.7.2 parser and installed physical gameplay, geometrically matching station doorway/hangar/cabin, multiple-hull picker, robust tamper/recovery and large original regression suite. True station-owned refinery issue #116, graphical equipment-checked ship mining #117 and asteroid collision #114 remain independent.

This is stacked on draft PR #118 (mining/refining native test), itself stacked on #113 (cockpit flight). Do not merge/claim installed acceptance before inherited dependencies pass.
