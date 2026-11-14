# FP3D-02D — Windows/F7 reliability patch (source-only)

This is a SMALL SOURCE OVERLAY for the newer FP3D-02B native desktop bridge
package, not a new compiled game. Extract only onto a separate copy of the
FP3D-02B `desktop_godot` folder; preserve your ongoing FP3D-02A builder run.

Changes:
- Windows Build-Windows.ps1 uses curl.exe HTTPS resumable .part caches,
  3 attempts and bounded retries, exact official GitHub release SHA-256/size.
  It can reuse an installed Godot **4.7.2.stable** editor on PATH, or accept
  `SG_GODOT_EXE` environment variable pointing to your installed editor.
  Installed editor provenance is explicitly unverified, not called official.
- F7 now toggles the read-only bridge for a bounded 600 ticks (~30 seconds at
  50ms/tick) rather than 120 ticks (~6 seconds). On completion or error it
  keeps the last verified contacts and a stable reason in the HUD.
- Godot writes startup/rejection/exit reasons to the local Godot user-data log
  `user://sg_native_bridge_status.log`; nonempty native stderr is displayed.

Source-level Python tests: 20+20+17 checks and distribution check pass.
Godot GDScript parsing and actual Windows runtime have NOT run here.
The screenshot's exact download failure is still UNKNOWN until Windows
`SingleGalaxy_Windows_Build_Evidence.zip` is available. Do not claim that
resumable downloads cure every network or export-template failure.

Important: This patch contains NO gameplay commands, NO native SQLite
mutations from Godot, and NO official Windows exporter executable. To test
with the native bridge, use the already-delivered FP3D-02B package.

If the old Windows build failed after a large download, keep its `.part` file.
The new builder can resume it. Do not rerun the old builder, which deleted
`.part` files on launch.
