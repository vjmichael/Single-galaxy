# Single-Galaxy — Standalone Graybox FPS (FP3D-02A)

**Status:** source implementation for a genuine native-window Godot 4.7.2 project; packaged Windows `.exe` and actual Godot runtime tests **not yet available**. This directory is **not** a web application. No browser, Python, local HTTP service, Docker, textures, external assets, or installed extensions are used.

## Why Godot

Creator chose to move off the browser to a standalone desktop first-person game. Godot 4.7.2 stable supplies first-person camera, frame pacing, 3D physics, Windows/Linux export, and eventual 3D meshing without maintaining an invented renderer. Standard GDScript; no C# or .NET required. The export is intended to include the Godot runtime; the user should not need to install the editor to play.

## Current functionality

- Local *graybox* person can walk, sprint, jump, look around, collide with floors/walls/crates/low-poly ship, and inspect objects within 3.2 meters.
- Single monochrome material for everything; primitive shapes only; no hands, arms, character animation, models, sounds or textures. Low-poly cylinder NPC placeholder.
- Physical held keys and mouse input, not browser keyboard auto-repeat.
- WASD walking, mouse-look, Shift sprint, Space jump, E inspect, Esc releases mouse. The arrow keys **do not cause thrust** in this on-foot mode.
- 60 Hz physics, compatibility renderer, no external downloads or runtime assets.
- A deliberately prominent in-game label states that movement is **local prototype only** and **not connected to authoritative C++ galaxy**. No pretend persistent saves, cargo/custody, combat, mining or ship movement.

## Open the source (for development)

1. Install/open the official Godot **4.7.2-stable** editor.
2. Import `project.godot` from this directory.
3. Press F6/F5 to run the main scene.

The finished deliverable will be a standalone Windows .exe exported with the official Windows export template; that export/runtime verification is still a gate, not part of this source package.

## Authoritative C++ game integration — next work

The existing Single-Galaxy C++ kernel, SQLite persistence, operation journal, user permissions, finite inventories, mission/worker supervision, save/restart and world simulation must remain the **single source of truth**. Do not infer that local Godot collision/body position changes are authoritative.

1. **FP3D-02B:** Build a bounded native on-foot `PersonPoseXYZ` and motion command with ownership, gravity, collision & streaming cell, schema migration and SQLite journal recovery. Reject forged poses, cross-ship/cell teleports, invalid carry weights.
2. **FP3D-02C:** Implement a reviewed native protocol bridge from Godot **directly** to the C++ host, with authenticated locally scoped command submission and verified server-committed frames. Do not use Python relay or embedded browser.
3. **FP3D-03:** Native-authorized board/disembark determines input mode: WASD first-person movement when on foot; held WASD steering and ↑/↓ propulsion when piloting; B native counterthrust. Keyboard focus loss cancels new thrust, not physical velocity.
4. **FP3D-04:** Native raycast/line-of-sight interaction, physical FPS mining/combat, pickup and NPC presence, source-revision-aware saves.
5. **FP3D-05:** Export real Windows and Linux desktop builds; execute with actual hardware, check collision, frame pacing, profiling, four-cell travel and durable save/restart.

**Do not merge the desktop preview as full-game acceptance or discard the open R5 journal recovery failures.** Native C++ remains under draft PR #100; the browser-only preview in #102 is superseded as the main user interface but stays useful for debugging.

## Unit/source-level tests

`python3 tests/test_project_contract.py` runs static source checks. They are **not** proof that Godot's runtime or a Windows export has executed. The next gate requires the actual 4.7.2 Godot editor and matching export templates.

## One-click Windows test and export path (FP3D-02A-WINTEST)

Double-click `BUILD_WINDOWS.cmd` from this unpacked source tree on Windows 10/11. The script uses only PowerShell and official Godot 4.7.2 release binaries: it downloads the pinned editor and export-template archive with release SHA-256 verification, runs actual Godot import and headless scene tests, exports the Windows x64 executable, and writes a verified-PE build status. The desktop app runs without the editor after successful export.

The template archive download is large (about 1.2 GB) unless matching templates are already installed. **This automation has not been run on Windows here**; any Godot or export failure will be recorded in `SingleGalaxy_Windows_Build_Evidence.zip`. It will not automatically install anything else or access your native galaxy save data. See `build_tools/WINDOWS_ACCEPTANCE.md`.


### FP3D-02B: real native C++ read-only feed (optional)

Press **F7** in the graybox to start a child process named
`native/galaxy_desktop_readonly_host.exe` on Windows (or the Linux equivalent).
The C++ process owns a durable SQLite save in Godot's user data directory,
advances the actual simulation, decodes and independently verifies canonical
SGVIEW frames, and emits strictly bounded `SGDESK|1` snapshots through a
local **stdio pipe** (no HTTP, Python relay, or browser). Godot consumes the
observed ship and contacts and draws monochrome **non-colliding marker spheres**
relative to the native ship in the scene. The marker Y coordinate is only
illustrative; native world X/Y in millimeters become 3D viewer X/Z.

**This is not yet authoritative on-foot movement, boarding, mining or combat.**
The local FPS controller never writes its position into the native game. Native
host standard input is intentionally not parsed as game commands. Stream
identity, sequence and masked ownership are checked before display. The
native host exits after 120 committed 50ms steps to bound the test. Click F7
again to start another read-only session from the same persisted SQLite save.

Windows preview imports `winsqlite3.dll` (Windows 10/11) and Microsoft VC++
x64 runtime DLLs; they are not bundled. Real Windows child process startup
and Godot pipe-read acceptance remain **OPEN** until independently observed.

For an independent **real Windows native process test**, double-click
`TEST_NATIVE_BRIDGE_WINDOWS.cmd`. It launches the actual C++ executable twice
against a fresh temporary SQLite save, checks ticks 0–3 then 3–5, and creates
`SingleGalaxy_NativeBridge_Windows_Evidence.zip` for review. It does **not**
claim Godot UI success or authority over the local FPS character.

## FP3D-02G: editor-safe native controls

Godot's editor reserves **F7** to pause and **F8** to stop the running game, even during play testing. The game therefore uses **P** to start/stop persistent native FPS movement and **O** to start/stop the read-only observer. **WASD** remains walking, **Esc** releases mouse capture. Do not map runtime actions to F7/F8; pressing them in editor-driven sessions may pause or terminate the project. The exported standalone game and actual Windows reconnect/save behavior remain separate runtime acceptance gates.

## FP3D-03A: authoritative in-ship cockpit seat/stand (development source)

New source-only increment on top of the editor-safe P/O controls:

- **P:** connect/disconnect original native authoritative movement; **WASD:** native walking when on foot; **E:** request sit/stand near the prototype cockpit at native X/Z within 600 mm of interior origin; **O:** read-only observer; **Esc:** release cursor.
- The native C++ host now requires a reviewed **13-field POSE/ACK** response containing both the original walking sequence and a separate immutable seat-transition sequence. Godot changes mode only after verified native ACK. A seated person cannot walk or jump; there is not yet ship thrust.
- Native world STEP, cockpit receipt and seat state commit atomically; SQLite restart and tamper-check tests are in source. The game scene's visible ship is **not yet spatially aligned** with the native cockpit test room, so this is not a finished in-world cockpit interaction.
- A Windows host compiled for the previous 11-field protocol **must be rebuilt** to test this source. The Windows export gate now checks the local native child protocol and SQLite restart before bundling a host and declaring it compatible.
- The creator is testing directly in an already-installed Godot editor; **do not require BUILD_WINDOWS.cmd** to launch or test the GDScript scene. Actual rebuilt Windows host, Godot gameplay, and installed export acceptance remain pending.

See [the FP3D-03A source and acceptance ledger](../docs/engineering/FP3D03A_COCKPIT_SEAT_SOURCE.md). Controller support and remappable bindings remain deferred to [issue #111](https://github.com/vjmichael/Single-galaxy/issues/111).

## FP3D-03B — Native original-ship flight controls (source development)
The prototype now links the native cockpit seat to **the original C++ ship simulation**, not a fake Godot motion model. With P native session active and E seated, A/D sends original ship yaw, W/S sends lateral strafing, Up/Down sends forward/reverse thrust, B sends counterthrust, U requests original undock. The HUD reports the **actual** original ship's position, velocity, heading and dock state from validated native SHIP packets. Godot editor F7/F8 remain reserved and controller remapping remains deferred.

This source revision requires an **updated native C++ player host executable**. An older pre-FP3D-03B Windows host is incompatible even if it launches. The Windows builder performs a real native command/restart smoke and fails rather than labeling such a host compatible. Source-only review is not Windows acceptance. The creator already has Godot installed; **BUILD_WINDOWS.cmd is not required for in-editor play**. Full C++ compile, Godot runtime, pilot movement/camera geometry and original regression evidence remain open. See the engineering [flight ledger](../docs/engineering/FP3D03B_ORIGINAL_SHIP_FLIGHT.md).


### PLAY-STATION-02: Windows visual acceptance without a new Godot download

**Development source, not yet installed-Windows accepted.** From the exact current PR #119 source checkout, double-click `RUN_STATION_PLAYTEST_WINDOWS.cmd` instead of `BUILD_WINDOWS.cmd`. It locates (or opens a file picker for) your **already installed** Godot 4.7.2 Windows editor. It does **not** download an editor or the approximately 1.2 GB export-template archive. A compatible Windows C++/CMake/SQLite development environment is required because the script recompiles **both** native writer hosts from this source; it never silently reuses an incompatible older player/seat executable.

The script performs a real original-world station/boarding/cockpit native process acceptance, Godot project import, the repaired recursive-collider smoke, a separate two-room visual/hidden-collision smoke, and station/player protocol parser tests. Only if those pass does it open the interactive game.

Run this intended visual scenario in the opened game: **H** station native; **WASD** from the persistently recorded station spawn to the highlighted hangar hatch; **E** board; **P** switch to the original ship cockpit; **E** sit; **U** undock; **A/D** yaw; **Up/Down** thrust; **B** counterthrust. After docking back at the station, switch P→H and press E to disembark. The full one-tonne asteroid mining/refining game loop remains a different acceptance gate. Godot creates bounded client-observed `PERSON`, `POSE` and `SHIP` projection records in `user://sg_station_visual_acceptance.jsonl` (not a second authoritative world). A Windows run saves source-build and native/engine logs, a JSON manifest and copied client observations in `SingleGalaxy_Station_Visual_Windows_Evidence.zip`. An observed native frame chain is **not** a substitute for manually checking navigation, camera, collision visuals or usability.

The new tests only certify actual Godot runtime behavior after someone runs the real installed editor. Python source checks alone must not be reported as graphical acceptance. If Windows compilation cannot locate MSVC/SQLite, the script fails with explicit logs and does **not** declare a pass. Set `-Generator Ninja` when using an existing preconfigured Windows C++ development shell instead of Visual Studio's default generator.
