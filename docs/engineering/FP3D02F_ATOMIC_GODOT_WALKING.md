# Single-Galaxy FP3D-02F — native journal + Godot WASD bridge

## Scope
The authoritative original C++ `PersistentWorld` now commits each accepted FPS walking step in the **same original SQLite BEGIN IMMEDIATE transaction** as the world snapshot, operation-log STEP, and physical player's native interior pose/receipt. Aborted operations roll all of these changes back.

The standalone Godot client gains F8: it launches `galaxy_desktop_player_host` through child stdio, derives up to 99mm planar movement from held WASD and current yaw, and changes the local avatar's displayed position **only on native ACK**. Its own local FPS movement is suppressed during F8. F7 continues to be a separate read-only contact diagnostic. No browser, Python, MCP or network server is involved at runtime.

This first prototype's native player is the original registered player owner 7, aboard their owned hull in a bounded 16m monochrome test room; native capsule and fixed crate geometry are **NOT yet spatially identical** to the wider Godot showroom collision scene. Vertical walking, authoritative yaw, disembarking, ships, multiplayer, third-party controls and AI-generated movement remain separate future gates.

## Evidence actually executed
- Linux GCC 14.2, SQLite 3.46.1: C++ game kernel, FPS interior library and desktop command executable built and linked.
- Native CTest `galaxy_fp3d02c_interior_authority` PASS: original world STEP/snapshot/pose/receipt all rolled back on two injected fault stages; committed pose survives SQLite restart; tampering and unauthorized move rejected.
- Native CTest `galaxy_fp3d02f_local_authorized_pipe` PASS: local child pipe session and request sequencing, speed, replay and input denial, actual movement, EOF cleanup and same saved-world restart.
- Python **source-only** contract for Godot ACK movement and export integration PASS. Godot 4.7.2 GDScript/runtime/Windows F8 live movement has **not yet been executed in this environment**.
- New Windows x64 native executable built with Clang 23.1.5 + MSVC 14.44 and SDK; PE32+ AMD64 dependency inspection PASS. **Actual Windows native command execution not yet tested**.

## Windows test
Extract `desktop_godot/` to a clean folder. The native PE is already in `desktop_godot/native/`.
Double-click `desktop_godot/TEST_NATIVE_FPS_WINDOWS.cmd` to exercise actual native startup, authorized step and restart and write evidence.
For the Godot scene, open `desktop_godot/project.godot` with the matching Godot 4.7.2 editor and run. Press **F8** to connect to the C++ authoritative interior. The on-screen native state updates from ACK. F8 toggles off; WASD continues in local demo mode only after explicit disconnect. Debug status is retained in `user://sg_native_player_status.log`.
`desktop_godot/BUILD_WINDOWS.cmd` exports using official (hash-verified) Godot editor/template assets and bundles the PE next to the game when present. No Godot editor is required to play an exported game. Building/exporting and real Windows GPU acceptance remain open.

## Security/authority
Stdio child handles are local to the launching desktop process; the native host selects the original player identity from trusted persistent game state, not from untrusted Godot-supplied owner IDs. Session nonce, ordered request IDs, bounded speeds and per-commit C++ player/ship custody are enforced. This local single-player test pipe is **not** a remote-user authentication protocol. The prior FNV movement receipt digest is a consistency check, not a cryptographic signature. Full production movement collision/nav/seat/trust remains gated.
