# FLIGHT-01 — Single Canonical Autonomous NPC Flight Navigation Service

**Implementation:** Mission-neutral NPC flight service with one real, already existing celestial-transit executor, journaled in canonical `Simulation` snapshots v95 and the same `PersistentWorld` SQLite writer. This is a stacked draft above WORK-01C; actual Windows/Linux GitHub native CI needs exact-head acceptance before it is called verified.

## One service, multiple NPC work consumers

Flight navigation is a **shared capability**, not something owned by freight or rescue. The durable `NpcFlightOrder` has explicit owner, actual existing ship, actual named NPC pilot, destination celestial body, unique original transit receipt ID, purpose label and phase. Purpose is typed: General, Freight, Rescue, Passenger, Survey, Patrol, Mining. Every purpose runs the **same physical navigation validator and executor**; a label never authorizes goods, NPC transfer, medical treatment, mining, combat, distress dispatch or a rescue.

`PersistentWorld::queue_npc_flight(owner,ship,pilot,destination_body,flight_id,transit_request,purpose,failpoint)` is the authoritative admission API. A pilot ID of **zero** means the service deterministically selects the lowest available qualified named shipboard NPC from the *existing* world. A supplied nonzero pilot must be qualified as well. It checks the original ship owner, commissioned onboard colony ship, existing life-support identity, fitted scanner, original recognized astronomical destination, original physical frame, current docking and jump states, existing transit/operation conflicts, same-owner physical NPC custody, residency, health, shuttle assignments and any active navigation duty. No employee or ship is invented.

A mission owner must have actual ship authority before granting the NPC permission to navigate. The public work board/UI may later construct this exact typed request; there is no browser-dependent movement, screen scripting, separate agent database or duplicate ship-control command.

## Native executor

Existing `PersistentWorld::begin_celestial_transit` and `complete_celestial_transit` have been refactored to use **one shared pair of private Simulation physical helpers**, now also called by the unified navigator. These helpers preserve the original rules: registered frame, same-system destination recognized by canonical celestial catalogue, scanner fitted, not docked/jumping/active mining, no left-behind shuttle pilot/flight, no conflicting command, no unresolved ferry custody, unique transit request ID, capped pending/finished receipts, host geometry, exactly four committed transit ticks and original local-region construction. The original external host still controls manual travel with its SQLite wrapper, and cannot commandeer a ship currently reserved by an NPC flight.

The generic navigator advances inside the **same single copied `Simulation::step`** that is atomically committed by `PersistentWorld::step`:

1. **Planned:** The named real pilot is reserved against other native duties. In each native tick, if original physical preflight passes, the same original `PendingCelestialTransit` is created. Unavailable medical/shuttle duty waits; loss or reassignment of a named pilot cancels only the unstarted intent.
2. **InTransit:** Wait until the original `due_tick`; apply the exact same canonical body/frame transition as host travel. The pilot does not independently operate a second position system. No cargo or passenger stock moves in this step.
3. **Arrived:** The original `CompletedCelestialTransit` exists with the exact ship, owner, body and completion tick. A replay cannot apply the transition again. The pilot duty reservation releases, and downstream work (e.g. WORK-01C freight unloading after actual berth) can act at the next committed tick.
4. **Cancelled:** Only a genuinely *unstarted* planned flight may be cancelled; no deletion or instant reversal of an already active physical transit. A cancelled receipt remains as durable provenance and IDs cannot be reused.

No wall clock, client path solver, offscreen animation, inferred route permission or separate ship population is involved. Flights resume across host restarts and SQLite failpoints using the same canonical source maps. Personnel, ship cargo, life-support, NPC family custody and regional inventory remain untouched by navigation. An already started native voyage may continue even if the originally assigned pilot subsequently becomes a casualty, because the original physical departure was previously committed; no fictional replacement crew is recruited.

## Snapshot v95 and anti-forgery

New rows are `npcflight|id|owner|ship|pilot|destination|request|purpose|phase|posted|started|resolved`. v95 is **additive** on top of v94, with no replacement for v72 physical `frametransit_pending` and `frametransit_done`. The existing world writer, failpoint matrix and replay parser remain authoritative.

At snapshot restoration, the original ship owner, original NPC/tombstone identity, unique request IDs, active ship/pilot exclusivity, phase chronology and original transit evidence must reconcile:
- Planned/Cancelled orders have **no actual pending or completed transit**.
- InTransit orders reference **exactly one matching original native pending transit**, with matching owner, ship, destination and start tick.
- Arrived orders reference **exactly one matching completed physical transit**, including matching owner, ship, target and exact completion timestamp.
- Forged phase advancement without corresponding physical transit is rejected.
- Stale pilot duty cannot be promoted into a fictional completed flight.

The original `PersistentWorld` copy-and-commit boundary guarantees atomicity for posting, starting, completing and cancelling navigation alongside the original world snapshot. No separate flight journal or new cargo state exists.

## Real native end-to-end acceptance

`tests/PLAY13D_RegionalFacilitiesTests.cpp --npc-autonomous-nav` reuses the actual verified full planetary/freight scenario. It posts an existing NPC-operated planetary nutrient pickup, seals physical nutrients in a real carrier's finite inventory, queues one **generic Freight** NPC navigation order and checks its named pilot, authorization denial, double-book rejection, prevented manual takeover, v95 snapshot restart, SQLite fault after snapshot write, in-flight pending proof, forged arrival rejection, real native completed receipt and nonduplicating destination unload by WORK-01C.

The same physical carrier then makes a second independent voyage through the identical generic navigation service; its real goods arrive in the original recipient life-support stock. The same test separately exercises an existing **receiver vessel** through the same navigator using automatic named-pilot selection (zero requested pilot), a cancellable Survey purpose and a successfully completed Rescue-*purpose* trip. The Rescue label does **not** perform a rescue or alter custody. All original resource mass and NPC headcounts remain conserved; the test reloads the exact persisted world with every original and NPC flight receipt.

CMake: `galaxy_flight01_unified_npc_navigation`. GitHub Actions: `.github/workflows/flight01-unified-npc-navigation.yml` Windows/Linux native C++/SQLite. Its matrix also runs inherited physical WORK-01C and WORK-01A/B, plus original colony safety tests. **Source exists; native exact-head CI must still pass.**

## Single navigation service extension contracts

This is the **first physically executable adapter**: same-system inter-body celestial frame navigation. Additional adapters belong behind FLIGHT-01, never as independent NPC autopilots:

- **Local approach, station rendezvous, docking, undocking, asteroids and ship berthing:** use native thrust/heading, collision envelope, dock-state and authorization.
- **Gates, hyperlanes and fold routes:** use their installed, directional, financed and active physical transition state; never infer a network edge from visual proximity.
- **Planetary shuttle and atmospheric flights:** call original PLAY-13D shuttle custody/receipt APIs and keep passengers, pilots and cargo manifests under existing authority.
- **Inter-system travel:** only via real existing transport capabilities with checked fuel, NPC duty, system access and restart receipts; no free warp.
- **Emergency rescue:** navigation may take a ship to a site, but rescue itself still needs existing host-approved family, custody, supply and medical gates.
- **Player UX:** an explicit “NPC pilot this ship to…” work request should show planned/current/arrived/cancelled status, named pilot, destination and verified native receipt; direct player control must never fight autonomous control.

Not claimed by FLIGHT-01: a finished all-mode 3D autopilot, cross-system planner, dynamic obstacle avoidance, player-facing route editor, unprompted NPC destination choice, mission-target acquisition or general emergent transport-market AI. These build on this **single native service** as each original physical adapter is verified.
