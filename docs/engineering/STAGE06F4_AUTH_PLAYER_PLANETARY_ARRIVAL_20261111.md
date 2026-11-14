# S6-F4 — Original Player-Person Planetary Arrival and Re-embarkation

**2026-11-11. Branch:** `sg/stage6f4-player-planetary-custody-20261111`, stacked on original S6-F `4be4717`. **Status:** native source implemented and registered regression authored, **not compiled or executed**. No CI submission, PR merge, release or physical Radeon acceptance authorized.

## Scope and one writer

The original `Simulation` and `PersistentWorld` own the ONLY physical player and shuttle custody. There is **no new player registry, off-chain travel table, independent Godot person, new planet, cargo movement, or inventory**. v131 adds fields to the existing `PlayerPerson`, existing canonical snapshot and existing `PersistentWorld::Store::commit` transaction. Older v130 and earlier player records remain readable with zero additional planetary fields. The original v81-v130 NPC, shuttle, source, geological transport, settlement and ecology schemas are otherwise unchanged.

### F4-A — Authenticated player booking

- `PlanetaryPlayerAction::Board` is a **new explicit owner operation**, not inferred from passing into the Godot terrain collision or from a shuttle sighting. An existing registered `PlayerPerson` must be physically aboard their own ship, in the original planet body frame, undocked, with an operational *constructed* site, real physical shuttle and assigned named NPC pilot. Board uses one player seat from the existing three-seat budget, separately from the original NPC manifest. No NPC is created or cloned.
- `PlanetaryPlayerAction::Disembark` is only accepted after the original `PlanetaryShuttleState` reaches flight phase 2 (landed), has a matching original one-use descent receipt, and the human was actually aboard that same shuttle. The player becomes `AtPlanetarySurface`, but no movement or construction ability is inferred.
- `Reboard` accepts only that original physically landed shuttle and same player/site. `Exit` releases the person to the original parent ship only when the real original shuttle is in orbit again; if a physical descent occurred, it requires the matching completed original ascent receipt. Before a first launch, a user can cancel their original boarding without inventing a landing.

### F4-B — Durable physical transit

The existing `Simulation::launch_planetary_shuttle` continues to debit **real finite water propellant**, check owner/ship/body/pilot/NPC and dependent custody, and book the original immutable flight request. If a real player has boarded this shuttle, that same transaction changes `PlayerPerson` to `PlanetaryTransit` and records the actual flight request ID. The **existing three-tick** `process_planetary_shuttle_flights` changes the player back to `InPlanetaryShuttle` only at the same tick as the original NPC pilot and its manifested passengers reach their genuine destination. This original process sets the completed descent or ascent receipt. Missing or mismatched custody fails closed.

The original `begin_native_celestial_transit`, `queue_npc_flight` and `PersistentWorld::begin_celestial_transit` reject a ship departure while its original player is in the shuttle, in planetary transit, or disembarked on a surface. A direct ascent with the player ashore fails until explicit reboarding. No local renderer can trigger an arrival.

### F4-C — Snapshot, restart and replay

- Canonical `galaxy-kernel-v131` serializes existing player fields plus exact original planetary body, 36×72 region, revision, last committed flight, last descent and ascent receipts. A v131 player planetary transition is restored only if the ORIGINAL owner ship, built site, matching source-shuttle, pilot, flight phase and active flight/descent receipt all agree. v130 and older records decode unchanged.
- Bounded `playerplanetreceipt` one-use action IDs are included in the original snapshot and swapped atomically. Flight requests, original submitted commands, family approvals and player-transport IDs cannot be replayed as another kind of departure. The persistence wrapper also checks the original station receipt ledger for reused IDs.
- `PersistentWorld::planetary_player_shuttle_action` performs copy-and-commit with conserved material/credits, full canonical reparse, original SQLite generation and failpoint rollback. There is **no alternate writer**. Operator recovery reads the same `PlayerPerson` and original shuttle journal; an interrupted original descent resumes through the original world tick, not a client clock.
- `Stage6AuthorizedLanding.hpp` adds a read-only `NativePlayerSurfaceObservation`. The existing Godot packet reports `player_surface_verified` and exact original `player_disembark_receipt` only when the user genuinely disembarked. Rendering remains local inspection; not native walking or a teleport.

### F4-D — Authenticated live host interface

`src/colony_live_host.cpp` recognizes:

```text
PLANET_PLAYER_REGISTER
PLANET_PLAYER|BODY|LAT|LON|STATUS
PLANET_PLAYER|BODY|LAT|LON|BOARD|RECEIPT
PLANET_SHUTTLE|BODY|LAT|LON|DESCEND|FLIGHT_ID
PLANET_PLAYER|BODY|LAT|LON|DISEMBARK|RECEIPT
PLANET_PLAYER|BODY|LAT|LON|REBOARD|RECEIPT
PLANET_SHUTTLE|BODY|LAT|LON|ASCEND|FLIGHT_ID
PLANET_PLAYER|BODY|LAT|LON|EXIT|RECEIPT
```

All commands are gated by the existing authenticated `principal` and its original `ship`. `REGISTER` explicitly admits an existing owned original ship into the original person registry, if not yet registered. `PLANET_PLAYER_STATUS` is read-only and owner-scoped; `PLANET_PLAYER_COMMITTED` gives exact request and generation only after a committed transport action. Flight transitions still require the original full shuttle authorization/worker passage and durable ticks.

### Source-level test matrix (NOT RUN)

`tests/STAGE6_PlayerPlanetaryCustodyTests.cpp` and `galaxy_stage6_player_planetary_custody` CTest target stage:

1. Real original colony ship, finite mining/refinery, actual celestial body transit, constructed spaceport, named shuttle pilot and finite propellant.
2. Player registration; owner, key and premature disembark refusal.
3. Inject `FailPoint::AfterSnapshotWrite` on `Board` and require unchanged generation/world hash and a subsequently usable receipt.
4. Original `Board`, no vessel orphaning, journaled `DESCEND`, real three-tick in-flight state and independent SQLite backup/restart during flight.
5. Completed surface arrival, explicit `DISEMBARK`, exact Godot read-only native player receipt, wrong-owner and replay refusal, source-stock parity.
6. Refuse ascent while person still ashore, then explicit `REBOARD`, genuine original `ASCEND`, three-tick return and `EXIT` to the exact parent ship.
7. Original canonical v131 deserialize and persistent receipt replay refusal.

**No compiler, CTest, native host handshake, browser relay, original v86/v130 historical migration corpus, installed Godot Windows/Linux, or physical Radeon AI Pro R9700 test ran for this branch.** These are **open acceptance gates**, not reported passes.

## Remaining before S6-F4 is operationally accepted

- Real native host/relay-to-UI end-to-end invocation, replay of session disconnect and restart midway through the above state transitions, cancellation and safe reattachment.
- Exact-head Linux/Windows compilation and all planet/freight/WO3/WO4/WO5 regressions, including genuine historical SQLite copy and corruption/tamper tests; resolve demonstrated failures.
- Actual installed Godot user-facing descent controls, correct vessel presentation, no fake surface player and physical R9700 performance.
- **S6-F5** is NEXT: original planet-relative 3D person/NPC movement, server collision/contact, gravity/hazards and exact return-to-shuttle motion receipts. The isolated Godot `SurfaceInspector` remains explicitly **non-authoritative**.
- **S6-F6 / S6-G** remain downstream: continuous streamed planetary tiles, polar caps, native installed game integration, consolidated scale and production acceptance.
- **S6-D/S6-E** original cross-region geological movement, onward non-water freight, extended ecology, and all older acceptance remain open.

**No merge/release authorized.**
