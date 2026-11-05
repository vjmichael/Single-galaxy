# SHIP-01A.1 — Authoritative colony shipyard construction (2026-11-05)

**Implementation:** new stacked branch / draft PR.  
**Verification:** pending full exact-head native C++/SQLite Linux and Windows. Planetary UI decoder tests were executed separately and do not prove C++ acceptance.  
**Baseline:** SHIP-01A.0 preflight (read-only) on PR #51; PLAY-13D.4G freight on PR #50.

## Behavior implemented

The player initiates one construction order for an existing, player-owned colony ship, with two distinct available named NPC workers. The existing `industry::inspect_shipyard_build` preflight verifies authorization, colony stock and named workers. A **single authoritative world mutation** removes four real fabricated construction modules from `ColonyShip::modules` and creates a `ShipyardConstruction` record containing **eight ore-equivalent units of embodied structure**, making `Simulation::total_ore()` unchanged. It cannot allocate a new entity ID, spawn NPCs, or create a ready freighter.

One shipyard construction project per existing colony ship at this initial stage. Orders are keyed by unique, idempotent finite IDs; wrong owner, missing modules, unavailable/duplicate workers or an already-claimed colony are rejected. A vessel currently engaged in celestial transit cannot start construction. This first playable gate is colony-ship-associated infrastructure and will require a dedicated orbital footprint/entity and power model in subsequent work before claiming a complete shipyard.

`Simulation::step()` alone advances work. It takes eight successful committed ticks with the two original workers physically on the colony ship, assigned to that ship, available for duty and with the colony maintaining actual food/water/oxygen stock. Missing people, life support or a ship in transit **stalls** progress, not conjures an NPC or a completed facility. Completed projects retain their immutable construction receipts and cease accumulating progress. The nonpublic `process_shipyard_construction` is not a caller-writable progress lever.

## Persistence / security

Snapshot **v90** serializes the exact project owner/ship, two named IDs, embedded mass, started tick, current progress (0..8), latest progress tick and stalled ticks. Parser rejects malformed ranges, replayed order IDs, duplicate colonies and conflicting active worker claims; cross-verifies real owner and the referenced colony. Historical worker references can refer to live NPCs or real casualty records so later death does not invalidate a canonical save. `Simulation::swap` includes the new canonical map; `total_ore` conserves the four modules' original mass.

`PersistentWorld::start_shipyard_construction` uses the existing **copy → validate physical materials → SQLite commit → swap** transaction, including already-existing `FailPoint` hooks. A failed commit cannot debit modules or publish the yard. Existing journaled world ticks atomically advance labor and preserve restart consistency.

Live host commands: `SHIPYARD_BUILD|order|firstNpc|secondNpc`, `SHIPYARD_STATUS`. Client never supplies owner or colony ID; both come from authenticated session and world context. `SHIPYARD_STARTED`, `SHIPYARD_ITEM`, `SHIPYARD_DONE` telemetry derives only from committed authoritative state. Python HTTP relay rejects extra fields, bool-as-int, duplicate workers and invalid IDs. JS displays one global shipyard panel and never owns construction progress.

## Test gate added (not yet native-run at exact branch head)

`tests/SHIP01A_ConstructionTests.cpp` registered as `galaxy_ship01a_construction`:
1. Seed a real genesis ship, named crew and physical asteroid; material origin = real `Simulation::total_ore`.
2. Reject unauthorized, missing-stock, duplicate named worker and repeated construction.
3. Mine ore, refine and fabricate exactly four physical modules through native world commands.
4. Inject SQLite failure into `start_shipyard_construction`; assert identical generation/canonical state and unchanged module inventory.
5. Commit the build, verify **4 modules → 8 embodied ore** without world-matter drift.
6. Inject world-step SQLite failure; ensure no phantom tick, labor or generation.
7. Commit 3 work ticks, close process, reopen actual SQLite database, and verify exact saved v90 state and progress.
8. Reject forged v90 mass records, then progress five further ticks until operational. Verify no extra progress, free material or duplication on subsequent ticks.

New Node/HTTP relay tests check trusted telemetry framing and command authorization. **A green JS test does not validate the C++ world.**

## Important next gates

- **Native verification:** exact-sha compile and execute new CTest against the full current v90 game kernel, including Linux and Windows and sanitizer/restart checks. The older v66 local suite cannot validate a v90 world.
- **Power and physical yard:** attach a constructed orbital assembly berth/entity to a real stationary colony location, with power and equipment availability, rather than treating parent ship inventory as a complete yard.
- **SHIP-01B:** physically manufacture a freighter from components in the finished yard, with a finite slip, a build queue, a new entity registered once at completion, and real crew commissioning. No creation of an NPC or hull for free.
- **Contracts and expansion:** other colonies commission hulls; budget-limited NPC managers suggest/queue additional real yard builds only under explicit authorization.

**Status rule:** Do not label shipyard construction or freight gameplay fully verified until exact-source native build/CTest, durable SQLite recovery and player-facing test actually pass.
