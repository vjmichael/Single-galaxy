# FP3D-03B — Original-world cockpit flight integration

## Implementation scope
The native seat receipt now controls admission to real original C++ ship commands. InteriorMovement::pilot validates the registered original player in their owned ship, independently verifies the seated receipt and expected seat sequence, then selects one of the existing Simulation actions: Thrust (real yaw or acceleration) and RequestUndock (real departure). No Godot-side ship physics, browser service or client-supplied ship ID is used.

A friend-only PersistentWorld submission path rechecks the authoritative seat row under the original SQLite writer lock before persisting command acceptance. The original PersistentWorld::step then executes the command and journals the outcome and physical state. The game retains its ORIGINAL TWO JOURNAL RECORDS: acceptance followed by execution. They are not misrepresented as a single atomic command+step commit.

**Interrupted command limitation:** If the process is terminated after acceptance but before step, the original save contains an outstanding cockpit flight command. The host detects such a command on restart and REFUSES to run new controls or silently replay it. A reviewed recovery/cancellation transaction is still required; this is fail-closed source code, not completed recovery.

## Prototype controls (future remapping under issue #111)
- P: connect/disconnect native authoritative session
- E: cockpit sit/stand inside native seat zone
- A/D: original yaw left/right (15 degrees per applied command)
- W/S: port/starboard strafing relative to original heading; NOT pitch
- Up/Down: original forward/reverse propulsion relative to heading
- B: original opposite thrust against current physical XY velocity
- U: request real undock, then advance original docking transition
- On-foot WASD remains walking. Focus loss stops new inputs, not ship momentum

The original physics has authoritative XY yaw and velocity (and optional physical Z in newer snapshots), not full pitch/roll yet. This first slice rounds the heading to the nearest XY cardinal thrust axis. Counterthrust opposes velocity without overcorrecting components smaller than the original 250 mm/tick² acceleration quantum. Full native 6DOF remains future work.

## Verified data path
Godot sends only a fixed FLIGHT verb, host session, request sequence and expected seat revision. C++ chooses the original owned ship, issues real native commands, and returns actual original Entity ship state. POSE/ACK now reports current canonical world tick and generation rather than the last movement receipt timestamp, fixing seat transition acknowledgement staleness. A 17-field SHIP frame transmits real native position, velocity, heading, docking state, current tick/generation and original command outcome. Godot checks sequence, identity, numerical ranges and journal clock before showing telemetry. The cabin camera does not fake ship translation.

## Source and acceptance gates
- New CTest: galaxy_fp3d03b_cockpit_flight, including yaw, native thrust, strafe, counterthrust, undock, seat denials, persisted ship position and interrupted-flight detection.
- New CTest: galaxy_fp3d03b_native_flight_pipe, exercising native child stdio frames and restart.
- Existing FP3D02F pipe test updated for initial SHIP telemetry; Godot headless parser smoke and source checks expanded.
- Windows native acceptance script updated to inspect actual undock, yaw, thrust, braking, ship-state recovery.
- NOT YET EXECUTED: full Linux/Windows native compiler, CTest, real Godot parser/runtime, installed Windows game, complete older kernel regression suite. Existing native binaries from earlier source revisions cannot speak the new ship protocol.

## Follow-on
1. Run exact-head native Windows/Linux builds and repair actual failures.
2. Implement explicit host-authorized interrupted-command cancellation/recovery with journal proof.
3. Align physical interior, cockpit zone and original exterior ship view with the Godot graybox.
4. Extend true native pitch/roll and smooth analog steering with versioned simulation state.
5. Physical board/disembark and remappable controller support, then installed-game acceptance.
