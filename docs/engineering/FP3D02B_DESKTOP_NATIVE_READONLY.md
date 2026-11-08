# FP3D-02B.0 — Standalone Godot ↔ native C++ read-only bridge

**Basis:** stacked on Single-Galaxy PR #104 (Godot 4.7.2 graybox builder), with
native R5 simulation lineage from PR #100. No browser, HTTP, Python runtime,
or MCP is required in the actual game.

## What it proves

- Real native `galaxy_desktop_readonly_host` creates or reopens a SQLite-backed
  persistent world, advances its committed simulation, and projects an owner-7
  ship, stations and asteroid through `ServerViewBridge`.
- Before display, `ClientView` independently decodes and verifies canonical
  `SGVIEW` frames, sequence and projection hash on the **C++ side**.
- The native host emits complete bounded text `SGDESK|1` packets over its own
  stdout. Godot runs this native child with `OS.execute_with_pipe` only on F7,
  reads its stdout, validates stream ID/sequence/tick, owner and contact syntax,
  then renders uncollidable low-poly markers.
- Godot remains local graybox FPS for walking, standing and looking. No
  authoritative on-foot XYZ body, inventory, combat or ship control yet.
- Native host stdin is deliberately unused: client cannot submit game commands
  on this channel. There is no claim that the untrusted client is a permission
  authority.

## Actual test status (2026-11-08)

- Linux exact R5 C++ CMake/Ninja native target **58/58 build steps passed**.
- Linux native process+SQLite restart+refusal negative test **PASS**.
- Real Windows x64 Clang23/MSVC SDK build/link **58/58 steps passed**; PE32+
  x64 import table inspected. Windows `winsqlite3.dll`, VC++ runtime required.
- Source-only 16/16 defensive integration checks passed.
- Godot 4.7.2 source parser smoke test is included but has **NOT** run on
  our Linux authoring machine (no actual Godot editor available there).
- Real Windows native execution, Godot child process pipe reads, GPU interaction,
  full native 271 matrix (last 204/271), first-person native actor identity and
  anti-forgery / safety authority **remain OPEN**.

## How to test on Windows

1. Use a fresh source extract; if launching from Godot editor, put bundled
   `native/galaxy_desktop_readonly_host.exe` under `desktop_godot/native/`.
2. Double-click `desktop_godot/TEST_NATIVE_BRIDGE_WINDOWS.cmd` to verify native
   process/SQLite restart. Upload `SingleGalaxy_NativeBridge_Windows_Evidence.zip`.
3. Import `desktop_godot/project.godot` in Godot 4.7.2, run, press **F7**.
   The HUD must say `NATIVE READ ONLY`, then show a fresh tick and actual
   count of observed contacts; markers must not collide with the player.
4. To test a standalone exported game, run the updated
   `desktop_godot/BUILD_WINDOWS.cmd`: it copies the native observer next to
   the game `.exe` inside the generated game ZIP and also runs a native parser
   Godot headless test. Keep MSVC runtime installed. Do not overwrite an
   existing saved colony; the bridge uses a separate Godot user-data SQLite save.
5. Upload both Windows build evidence ZIP and native bridge evidence ZIP for
   independent interpretation. A successful build is not a successful GPU run.

## Next engineering gate

True native PersonPoseXYZ, high-order galactic 3D coordinates, on-foot collision
and authoritative input; first-person boarding state and typed native tool
admission; native journal operator-attribution and restart/anti-teleport tests.
The current read-only child is only the safe transport bootstrap.
