# FLIGHT-03 — Universal physical-object approach (incremental native slice)

Source branch: `feature/flight03-universal-object-approach-20261106` stacked on FLIGHT-02. Exact-head CI is required before acceptance.

## Architectural rule
One navigator, one authoritative ship physical state and native Thrust command path. `assist_to_object(ship,target,stand_off_mm)` is the shared local guidance primitive for ship, asteroid, station, wreck, or gate entity. It reads target **position and velocity on the current committed tick**, computes an offset waypoint and issues a desired two-axis thrust using relative velocity for braking. An approach completion means **relative** velocity is zero and the ship is inside the waypoint tolerance. It never means cargo, mining, rescue or docking succeeded.

The existing FLIGHT-02 NPC station control path now calls the universal guidance function with a 1500 mm standoff. The old `assist_to_station` remains for existing callers/tests.

## Separation of concerns
- The mission-authorized request must resolve target ID, visibility, system and frame custody and reserve the original NPC pilot and vessel.
- The pure navigator handles physical intercept and velocity matching. Ship motion remains the original `Action::Thrust` physics and bounded command ledger.
- Interaction adapters check physical distance and their own permissions: station native `RequestDock`, ship-ship berth/handoff, rescue boarding, mining, salvage or fleet formation.
- No teleportation, inventory mutation, synthetic crews or task-specific autopilots.
- Do not treat an asteroid as stationary by type: the native target velocity is authoritative.
- Collision/obstacle avoidance, rotating targets, radii and dynamic safe abort require dedicated acceptance beyond this first guidance slice.

## Verification / outstanding
Native test `galaxy_flight03_object_approach` covers stationary asteroid, station, wreck, moving freighter, matching target velocity, retargeting after movement, cross-system and self-approach rejection. Windows/Linux workflow additionally runs native FLIGHT-02 docking regression.

**Not yet implemented in this slice:** owner-authenticated generic target-order posting, persistent moving-ship rendezvous orders, berth/transfer receipts, full ship-to-ship collision avoidance, target despawn recovery and complete end-to-end freighter/rescue integration. Do not claim those complete based on a pure navigation helper.


## FLIGHT-03 native execution closeout — November 6, 2026

The earlier "not implemented" section above described the first pure-solver slice and is superseded for these items by the new v97 code:
- `Simulation::queue_npc_ship_rendezvous` and `PersistentWorld::queue_npc_ship_rendezvous` now admit authenticated actual-ship targets using the same existing NPC pilot/ship exclusivity and original SQLite copied-state commit as FLIGHT-02.
- `Simulation::process_npc_flight_navigation` now executes **LocalRendezvous** within the original shared navigator. It tracks the physical target ship every committed tick and uses its actual position and velocity. Only original native `Action::Thrust` controls move the approaching ship; no teleportation and no synthetic crew or inventory.
- When the ships already have matched pose and velocity at posting, an applied original no-op `Thrust` provides a genuine native control receipt before flight completion.
- Arrival produces `ActivityKind::NpcRendezvous` attached to the final accepted original control command. This receipt carries **zero goods** and confers no cargo, docking or passenger authority.
- Canonical snapshots use v97 for this mode while prior v95/v96 rows remain accepted. Active flights require a real target in the same frame. **Completed flights** validate their historical native command/activity receipts rather than requiring ships to remain spatially together forever.
- Invalid moving targets cannot produce false success; active orders cancel when target entity or physical frame is no longer valid.
- Native `galaxy_flight03_native_rendezvous` test exercises wrong owner, no self-approach, SQLite posting fault rollback, moving physical target, same-world v97 snapshot roundtrip, valid physical arrival receipt, zero invented material and SQLite restart. Windows/Linux GitHub Actions now build this fixture along with original station docking and pure object-guidance regressions.

**Acceptance status:** source committed. The exact-head native Windows/Linux CI jobs are queued; native success is not claimed.

**Adjacent full-loop acceptance:** FLIGHT-04B's stacked branch now adds `galaxy_flight04b_real_freight` to reuse the actual WORK-01C sealed intercolony freight scenario and original trusted delivery executor, with a real LocalRendezvous barrier, deliberately failed SQLite delivery commit, owner rejection, successful native sealed-stock transfer, duplicate refusal and existing final recovery. A prior *celestial flight* remains required by the established intercolony inventory authority. The same-room ship-ship freight path without a prior celestial voyage is still a separate future transport rule.
