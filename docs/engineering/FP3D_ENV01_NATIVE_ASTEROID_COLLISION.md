# FP3D-ENV-01 — original native swept asteroid contact (3D-first)

**Latest revision, 2026-11-09:** The initial 1-m / whole-tick stop
descriptions below record the historical 01A–01C implementation and are
**superseded** by 01D/01E: a 5.2-m whole-graybox-hull conservative proxy,
first-safe-millimetre native advancement, camera yaw and real Godot/SGDX
integration. 01F adds receipt-joined pilot navigation. These remain draft
pending actual exact-head Windows/Linux and physical Windows playtest gates.


Draft native implementation on independent world-first branch from PR #123 (04A), not experimental module PR #124. Creator intentionally defers all ship module features until 3D world is genuinely playable.

Version 125 is explicit new-world genesis opt-in after enabling actual high-order galactic coordinates; legacy world movement is unchanged. For each in-space original ship, before its normal v120 absolute galactic integrator advances, the kernel checks a bounded list of same-system original asteroids. A portable integer center-to-segment sphere sweep uses actual original ship velocity, x/y/z, asteroid geological radius and a provisional 1m ship collision sphere. Contact cancels only that tick's movement, sets all native velocity axes to zero, and produces an original durable `ShipAsteroidContact` activity event (source ship ID, asteroid ID, tick). It does not damage the ship or create resources. The exact original pose remains the sole authority and Godot only observes it.

The minimal obstacle radius is conservative; this **does not** claim realistic collision impulses, variable hull geometry, ship-ship/station collision, perfect asteroid meshes, or installed Godot visual parity. Those remain open issue #114 follow-on subtasks. Legacy saves never silently acquire different physics. No 04B equipment feature is imported.

**Native acceptance required:** compile original C++ on Linux and MSVC Windows, sweep a fast thin obstacle, glancing contact, tangent near-miss, full vertical separation and escape, then run original PersistentWorld to prove the ship cannot cross a real physical rock, no ore or hull gained, event persisted and exact canonical snapshot survives SQLite restart. Add spatial index/scaling, across-cell trajectory, real collisions in Godot, camera alignment and installed test after this first correctness slice.

## FP3D-ENV-01B: high-order boundaries and bounded nearby objects

A numeric sector or 100-km cell edge is not a barrier. Native collision uses the existing verified high-order `galactic::nearby_displacement` on the original ship and asteroid absolute 3D positions. Added direct tests for opposite sides of the same sector seam in X, diagonal Y/Z seam crossings, local 100km cell boundary, a distant sector, and an intentionally unrepresentable sector overflow. The real SQLite ship/asteroid fixture is now anchored across the high-order X sector seam and verifies it cannot penetrate the rock on the other side. The narrow-phase cap counts only spatially relevant rock colliders, rather than every distant asteroid in the same star system. It still performs a full per-ship entity scan, so broadphase spatial indexing and large-system latency remain open and must not be described as solved.

The integer helper deliberately uses a conservative collision skin and stops the current tick, without fictional hull damage or changing the authoritative ship position. Full impact time resolution, physical hull shapes, ship–ship/station interactions, Godot frame parity and Windows installed acceptance are future gates.

## FP3D-ENV-01C — original contact visible without client authority

The original **fresh test station** genesis, plus the finite original-rock test seed, now explicitly enable v125 swept collision. An already-existing SQLite world retains its own earlier version; this change does not upgrade old games, refill ore or install ship modules. The cockpit C++ host reads its source `ShipAsteroidContact` event for the **same current committed tick** and appends rock ID/tick to original `ROCK` and `ROCKNONE` observation frames (two numeric fields, only for v125 worlds). Initial frames after reconnect contain zeros, never an invented or replayed impact. The existing `SHIP` frame and older 04A `ROCK` wire format remain unchanged.

Godot validates that an impact is from the already-verified owner/ship/session/request/tick, requires in-space custody, and displays an explicit solid-contact notice. Exterior rendering converts original native XYZ millimetres to Godot X/Z horizontal and Y vertical, anchored to the verified avatar/camera rather than the static station origin; no Godot collision body or alternate physics writer was added. An authentic child-pipe test runs the explicit native ship to the finite rock, checks the recorded event, stopped velocity, source/cargo conservation, denied replay, and SQLite restart. A separate Godot script tests both legacy and v125 synthetic frames, invalid event-tick rejection and metre/axis render mapping.

**Acceptance remains open:** source implementation is committed, but this log is not evidence that exact-head Linux/MSVC compilation, real child SQLite execution, real Godot 4.7.2 parsing, or physical Windows gameplay has succeeded. Inspect those jobs and repair genuine failures. The projected hull remains a provisional 1-m sphere; complex mesh hulls, other collision objects, truly accurate camera placement and broadphase indexing are future work.


## FP3D-ENV-01F — verified native flight-and-target instrumentation (2026-11-09)

The actual Godot 3D cockpit now displays **a read-only pilot target instrument**
computed from the same original C++ ship and asteroid observation frames as the
physical collision system. This is a navigation and gameplay-usability step
toward flying to a rock, equipping a real mining drill later, returning and
docking without inventing a second world or free resources.

**Committed integration:**
- New `desktop_godot/world/native_flight_instruments.gd` consumes only an
  authenticated original `SHIP` + `ROCK` / `ROCKNONE` pair with matching
  original ship ID, tick and SQLite world-generation, and with original ship
  `dock=0`. All observations are admitted upstream by the existing C++ pipe
  protocol validator; the pure UI helper repeats identity/epoch and dock checks
  to fail closed against stale mixing.
- A fresh native `SHIP` frame invalidates the previous asteroid instrument
  until a corresponding new `ROCK` frame is received. Disconnect clears both
  retained frames and replaces the readout with an offline status.
- Instrument shows native yaw, selected finite rock identity, approach bearing
  (turn LEFT/RIGHT/AHEAD), vertical offset (ABOVE/BELOW), range, whole-hull
  **clearance from the original conservative 5.2 m sphere + 1 mm contact skin**,
  and signed target-directed closing/receding velocity in original mm/tick.
  These are derived displays of *present* authoritative state, not movement
  prediction, collision resolution, AI steering, docking commands or mining.
- A **verified impact** label requires an original same-rock same-tick
  `ShipAsteroidContact` receipt. The UI never manufactures such an event.
- The live Godot scene test adds checks that the displayed instrument joins
  the actual original C++ host collision receipt and agrees with safe hull
  clearance/bearing. A separate Godot `SceneTree` smoke covers 15 synthetic
  identity, replay, tick, generation, stale, bearing, clearance, radial speed,
  collision and empty-target cases. Python
  `test_fp3d_env01f_pilot_contract.py` is **source-only**, and cannot
  establish native/engine/pass acceptance.

**Next original-world interactive gameplay steps:** end-group real Linux/Windows
native compilation plus Godot scene acceptance, physical Windows camera/GPU
playtest; station/ship/wreck collision layers and realistic hull contact;
then real manufactured ship mining equipment, exact one-tonne rock-to-cargo
transfer, return docking, unloading and refinery intake. Continue using the
one-task-group CI gate (no per-commit workflows). Ship modules remain
**deferred** until the 3D world passes physically installed game acceptance.

**Acceptance state:** implementation and test recipes are committed to the
draft branch, not yet executed on the exact new Windows/Linux PR head.
Do not treat synthetic/contract checks as genuine Godot/SQLite/GPU evidence.
