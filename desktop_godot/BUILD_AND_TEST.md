# Build/release gate: native Godot 4.7.2 stable

**Windows one-click build:** double-click `BUILD_WINDOWS.cmd`. It downloads pinned official Godot assets, verifies GitHub release SHA256, runs Godot project import and native headless smoke, exports x64 executable and writes a ZIP containing the build evidence on success or failure. See `build_tools/WINDOWS_ACCEPTANCE.md`. This script has not been executed on Windows in the current environment.

This is the **standalone game** transition, not a browser or Python relay.

## Reproducible tooling

1. Official stable Godot v4.7.2, non-.NET editor, Windows/Linux.
2. Official matching Godot v4.7.2 **standard export templates** installed in the editor. No templates were available in the current build environment; neither headless Godot runtime nor .exe export has been performed.
3. No plugins, extensions, textures, shader packs, Docker, node_modules, Python runtime for players, local web server, or network.

## Native editor + smoke gate

```
godot --headless --path desktop_godot --editor --quit

godot --headless --path desktop_godot --script res://tests/godot_smoke.gd
```

Read exit codes and log warnings. Treat any parser/static type error as a blocker, not a warning. For the real graphical test:

```
godot --path desktop_godot
```

Check held WASD movement; mouse look; release Esc; no movement on lost focus; walking collision with walls/crates/ship; 60 Hz camera updates; jumping; E inspect; mouse capture; arrow keys do not fire any thruster.

## Windows standalone executable

```
godot --headless --path desktop_godot --export-release "Windows Desktop" desktop_godot/build/SingleGalaxy_Graybox.exe
```

The completed `SingleGalaxy_Graybox.exe` should be launchable **without a Godot editor install**. Verify `PE32+ x86-64`, embed PCK, and include Godot license/attribution in the distribution when exporting. DO NOT publish debug template or claim it has passed hardware acceptance until an actual Windows machine runs it.

## Linux standalone executable

```
godot --headless --path desktop_godot --export-release "Linux" desktop_godot/build/SingleGalaxy_Graybox.x86_64
```

Check ELF64, execute under an actual graphical display, check frame pacing/collision, and run headless smoke independently.

## Still not accepted / native backlog

- No verified Godot graphics frame exists yet in this environment.
- This graybox demo is **not currently attached to native Single-Galaxy**; browser R4 and C++ galaxy simulator remain separate. The next native bridge must carry safe on-foot, ship and interaction commands, and verified committed state.
- Do not pretend local Godot person pose is synchronized, owned, or journaled; true XYZ person-body and boarding admission must be implemented in the native C++ kernel and verified with SQLite.
- Follow-up: SHIP-STATUS cyber operation attribution receipt, 67 native regression failures as of PR #100 baseline, Windows runtime, 256-tick four-cell test, and eventual real FPS combat.
