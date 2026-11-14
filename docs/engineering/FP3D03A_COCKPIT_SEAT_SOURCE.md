# FP3D-03A — Original-world cockpit seat/stand prototype

## Implemented source scope

The original registered player aboard their own ship can request a **cockpit seat transition** from the standalone Godot first-person graybox. This is **not boarding/disembarkation, ship thrust, flight or controller support**. It creates a deliberately bounded first intermediate step toward FP3D-03.

- **Godot:** Press **P** to start the native authoritative session. Held **WASD** requests native walking; **E** requests sit/stand while the player remains within the native cockpit's 600-mm X/Z interaction zone around the ship-interior origin. **O** opens the separate read-only view. These are provisional actions pending future remappable keyboard/gamepad controls (issue #111). Godot editor F7/F8 remain reserved.
- **Native ingress:** Same OS-local child stdio process receives strictly bounded `MOVE` or `SEAT` actions, with session epoch, increasing request ID, expected movement/seat sequence, bounded numerical fields, and one host-owned player identity. It returns one committed 13-field `POSE`/`ACK` frame with cockpit state and seat receipt sequence. No browser, HTTP, Python gameplay relay or direct Godot SQLite access.
- **Authority:** `InteriorMovement::set_seated()` validates original physical player/owned hull, current pose, proximity to seat, expected revision and fresh receipt. Each valid sit/stand advances a real original STEP and creates its immutable cockpit receipt plus current mode **inside the same world SQLite writer transaction**. Injected crash faults exercise both the receipt and update boundaries. Walking fails while seated, including if a compromised client submits `MOVE`.
- **Recovery:** On reopen, both the original walking history and cockpit transition chain are verified against committed original STEP journal entries; altered seat state is refused. Transitions use the same original-world generation discipline as walking.

## Limitations and mandatory next gates

1. **Native test execution:** C++/SQLite build, `galaxy_fp3d02c_interior_authority`, native child-pipe tests, restart/tamper/fault injection must run on the exact head. Source-only checks cannot close these.
2. **Godot and Windows:** Run Godot 4.7.2 parser/protocol smoke and actual installed Windows binary using an updated `galaxy_desktop_player_host.exe`; the previously built executable speaks the old 11-field protocol and is **not compatible** with this branch's 13-field protocol.
3. **Actual geometry:** The native 16 m test interior and cockpit zone do not yet match the Godot graybox room and visual ship. The seat is an **authoritative mode**, not proof of camera-to-seat animation, locomotion collision parity or real ship cockpit coordinates.
4. **Follow-up FP3D-03B:** Bind physical seat custody to an original ship piloting controller; separate on-foot WASD from steering WASD, arrows for propulsion and B counterthrust. Never fake thrust with local camera movement.
5. **Follow-up FP3D-03C:** Implement genuine physical boarding/disembarking tied to location, vessel custody, safe exit geometry, save/restart and multiplayer authorization; do not infer it from the in-ship seat toggle.
6. **Controller/key remapping:** Deliberately deferred to issue #111 and should use action-based mappings, conflicts, user preferences, focus suppression and native-authorized commands.

## Test receipts

**No real native compile, CTest, Godot runtime, Windows installation or hardware result is claimed by this branch's source changes.** Corresponding test source and Windows evidence scripts have been updated; their execution remains an acceptance gate. Existing accepted proof remains limited to the physical editor's WASD behavior observed by the creator on FP3D-02F.
