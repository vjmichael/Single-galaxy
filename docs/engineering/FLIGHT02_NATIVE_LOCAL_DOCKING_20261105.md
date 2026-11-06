# FLIGHT-02 — One Native NPC Navigator, Local Approach and Station Docking

**Status:** Source, CMake/CTest, Windows/Linux exact-head CI and SQLite-native regression fixture committed as a draft stacked on FLIGHT-01 PR #64. **No native CI green claim until jobs finish.**

## Shared architecture: do not make a second autopilot

FLIGHT-02 adds `NpcFlightMode::LocalDock` to the existing **FLIGHT-01 `NpcFlightOrder`** rather than introducing an `NpcDockingNavigator` or a parallel task queue. Both native flight modes now also call the **same** `Simulation::select_npc_flight_pilot` and `npc_flight_slot_available` helpers: there is exactly one qualified NPC pilot chooser and one active ship/pilot exclusivity gate. The original `Simulation::process_npc_flight_navigation` is still the sole native NPC flight executor. The same original `NpcFlightPurpose`, owner consent, qualified existing crew, pilot reservation, `PersistentWorld` copied-state commit and canonical snapshot recovery apply.

Two interchangeable physical modes now share that service:
- **Celestial:** original v72 four-tick body/frame transition with its canonical pending/completed receipts, unchanged from FLIGHT-01. v95 rows continue to deserialize.
- **LocalDock:** a station in the ship's current **actual same-system, same-local-frame** physical world. This adapter calls the **existing** `assist_to_station` from `src/Navigation.cpp`, whose ordinary two-axis thrust/braking guidance also drives the existing B5.4 manual navigation tests.

`PersistentWorld::queue_npc_station_docking(owner,ship,pilot,station,flight_id,purpose,failpoint)` creates one real owner-authorized v96 flight record with its existing qualified named NPC pilot or, if `pilot=0`, the lowest-ID available qualified shipboard NPC. It rejects wrong owners, unbuilt/unrelated stations, incorrect local frame or system, unauthorized ship, a permanently unavailable existing crew, active voyage/celestial transit/physical shuttle, already docked destination, outstanding competing ship commands, active mining, conflicting flight orders and duplicate IDs. This does not create a ship, a person, a station, fuel or inventory.

## Actual control, not coordinate interpolation

At each committed `Simulation::step`, the **same one flight navigator** handles local ship orders using **only original host-validated ship commands**:

1. If already docked at a different original station, submit `Action::RequestUndock`. Its original `process_station_transitions` manages the true 2-tick undock, physical release and offset.
2. When `DockState::InSpace` and `ShipJumpState::Ready`, call `assist_to_station` with the **actual current ship entity position/velocity** and original station coordinates. Submit ordinary `Action::Thrust` with returned `-1,0,+1` axes. Existing acceleration, velocity clamp, short-range motion and physics remain the only physical location authority; no NPC-specific movement engine.
3. When the original assist says stopped inside the physical docking envelope, submit `Action::RequestDock`. The ship may still enter native Docking and require two further physical transition ticks. The command's submission alone **never** marks the job as fulfilled.
4. Only after the actual `DockState::Docked` at **that** station does the existing flight order become `NpcFlightPhase::Arrived`. The named pilot reservation releases, leaving ship and material inventories untouched.

Original `Simulation::submit` creates **durable, replay-safe** scheduled command IDs under a bounded private namespace generated deterministically from the real flight ID and control sequence; original `execute` produces native command success/failure events; original station transition emits the native `ActivityKind::Docked` activity. Flight authority does not bypass these commands' `Outcome` checks. The host `PersistentWorld::submit` refuses competing manual thrust, docking, undock and jump commands **while an NPC flight owns that ship**; direct manual flight resumes once the original navigation order is resolved or cancelled.

Local control is capped at 511 native commands per order. An exhausted order transitions to Cancelled and releases the pilot without pretending the ship reached its station, moving material or teleporting position. `cancel_npc_flight` remains strictly predeparture; an underway local approach does not secretly interrupt a currently submitted physical command. More advanced owner-initiated safe brake/abort is a future explicit acceptance gate.

## v96 source-of-truth and replay rules

FLIGHT-02 adds **four fields to the same** `npcflight` record when snapshot version is v96:
`mode | station | local_commands | last_local_command`. Celestial v95 records remain valid with these fields zero in v96 and also load unchanged from exact v95 snapshots.

The v96 local-mode parser:
- Requires `transit_request=0` and `destination_body=0`, a real station target, correct owner, exclusive real ship and named pilot and bounded command count.
- Checks that a nonzero original `last_local_command` exactly matches the generated command ID for this order/sequence and exists in the **original submitted command ID** set.
- For `Arrived`, checks actual ship `DockState::Docked` at the requested station, that the last native command produced an `Outcome::Applied` event and the original station transition journal includes `ActivityKind::Docked` for this ship/station within the committed flight interval.
- For `Planned` and `InTransit`, preserves the existing phase/tick chronology. In-progress physical scheduled controls survive a SQLite restart and run only on their original `execute_tick`.
- Does not create independent cargo, passenger, flight position or action receipts. Both celestial and local modes still use one existing `npc_flight_orders_` map.

## Real native tests

`tests/FLIGHT02_LocalDockingTests.cpp` runs **three actual local trajectories**: diagonal, vertical and negative axes, each on a real starting colony ship with four individually counted, qualified NPCs and real finite life support. For each:
- Wrong owner, bogus NPC and nonexistent/nonstation target are rejected; automatic pilot selection is deterministic and cannot double-book the same real person.
- SQLite failed order posting and failed first active world step do not publish partial state; exact v96 predeparture and in-motion snapshots round-trip.
- The named pilot cannot simultaneously work elsewhere; manual thrust and docking cannot override an active NPC authority lease.
- Original two-axis thrust and braking bring the physical ship to the designated station, which then actually finishes the native docking transition and produces a recoverable receipt. A forged `Arrived` flag before the physical dock is rejected.
- A second order takes that same ship from its first actual dock, uses the original **undock** and native guidance to return to the other station; completed pilot reservations release.
- SQLite close/reopen recreates both physical station states, NPC identities and completed flight orders with no added stock/people, after which ordinary manual `RequestUndock` still works.

CMake: `galaxy_flight02_local_docking`. GitHub Actions `.github/workflows/flight02-local-docking.yml`: Windows/Linux actual native SQLite, FLIGHT-02, original B5.4 Navigation, inherited FLIGHT-01 celestial and WORK-01C transport/WORK-01B planner CTests.

## Future same-system adapters

FLIGHT-03 should continue with **local ship rendezvous and safe berth** (against physical moving targets where original rules support them), followed by native gate/hyperlane route edges. The original NPC pilot, owner, mission purpose, flight ledger and SQLite recovery stay unchanged. Do not add freight-only, rescue-only, patrol-only or mining-only navigation code.

**Not claimed:** native CI green status, installed Windows client UI acceptance, obstacle-avoidance guarantees, moving-ship pursuit, cross-system autonomous route finding, full free-space 3D navigation, automatic combat or rescue, or safe mid-flight user takeover with braking. Those require distinct physical acceptance gates, **not a different NPC navigation system**.
