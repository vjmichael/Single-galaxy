# FP3D-02A-WINTEST gates (2026-11-08)

## Completed by actual execution in the Linux authoring container
- Current Godot graybox source and export presets packaged.
- Twenty original source-structure and separation assertions passed.
- Twenty one-click build/download verification source assertions passed.
- Source distribution check passed.
- ZIP CRC integrity and per-file SHA256 manifest validated after packaging.

## Not yet executed; must not be marked passed
- No official Godot 4.7.2 binary available locally; no GDScript compilation through Godot itself.
- Godot headless physics smoke on Windows/Linux not run.
- Windows export template not downloaded/extracted in the authoring environment; no Windows PE exported here.
- No actual Windows GPU render, input hold, focus-loss, collision, or native installed-app acceptance.
- No Godot→C++ IPC, native person-state authority, persistent pose, or native ship boarding.
- The current native C++ full-suite baseline remains 204/271 passing from PR #100 (67 failing).

## Evidence expected from the user's Windows machine
Extract source ZIP, double-click `BUILD_WINDOWS.cmd`, allow downloads of official Godot 4.7.2 assets. If successful, use `RUN_GAME.cmd` or the generated `build/SingleGalaxy_Graybox.exe` to inspect the graybox. Upload `SingleGalaxy_Windows_Build_Evidence.zip` regardless of outcome. If the export succeeded, `SingleGalaxy_Graybox_Windows_x64.zip` is the self-contained source-independent executable bundle. Reports from actual mouse/keyboard/graphics use are a separate acceptance record.

If GitHub rate limits or download errors occur, retain the evidence ZIP. Do not bypass SHA256 verification. No Python or Docker is required to play or to run the builder.
