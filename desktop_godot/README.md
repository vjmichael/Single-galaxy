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
