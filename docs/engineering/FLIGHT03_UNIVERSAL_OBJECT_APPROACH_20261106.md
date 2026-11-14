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
