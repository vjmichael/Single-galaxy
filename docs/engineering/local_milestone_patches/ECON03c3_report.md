# ECON-03c3 — Opt-in Regional Event Scheduler and Notice Outbox

Date: 2026-11-01. Baseline: ECON-03c2 source package. Development was local, not in GitHub CI.

## Implemented

- Added `RegionalEventScheduler` as an **opt-in** deterministic region kernel, not yet integrated into `PersistentWorld` or any game UI.
- Cold-colony cohorts share a single persistent logical tick, but advance algebraically via `ColdColonyNeeds` between significant events. No per-resident or per-tick distant NPC iteration.
- Local deliveries and cross-colony shipments require **real, immediately reserved depot inventory**, placed in scheduled cargo custody. Departure and arrival occur at exact logical timestamps, with no tick-by-tick spaceship simulation. Cargo is never granted freely. Destination inventories are credited only after arrival.
- Population events change the size of a cohort on a defined tick. Events at a consumption deadline process **before** that deadline's ration deduction.
- Shortage notices are emitted once per continuous shortage episode at the first unsupplied consumption deadline; supply recovery notices mark the first subsequently supplied deadline. Shipment reservation, departure, arrival, habitat supply, and population changes generate notices.
- The pending notice outbox sorts by logical tick and can be drained in bounded batches; a future authoritative host must persist/acknowledge notice delivery, enforce permissions, and determine whether to display or push each notice.
- Advancing to a target is transactional within the kernel: failures such as resource overflow preserve previous state, scheduled events and notices.

## Verified locally

Focused `galaxy_econ03c3_regional_scheduler_tests` passed. The compatibility tests `galaxy_econ03b_market_recovery_tests`, `galaxy_econ03c1_macro_needs_tests` and `galaxy_econ03c2_cold_colony_tests` also passed, four of four total.

Tests cover material conservation through shipment transit and habitation; identical state after one-shot and split advancement; alert sets; outbox ordering; undersupplied cargo rejection; invalid arrival rollback; and a 1.3-trillion-person cohort running for one trillion ticks with one cohort evaluation.

## Not implemented / next steps

1. **Persist** scheduled events, reserved cargo, checkpoint state and notification outbox to SQLite under authoritative ownership; replay without duplicate arrivals or notification delivery.
2. Integrate with NPC work assignments and real ship route calculations. The `travel_ticks` argument represents a prevalidated route and actual moving carrier must ultimately be accounted for separately.
3. Extend the event algebra to production runs, utilities, market exchanges, waste reclaimers, trade authorization and NPC migrations. No extrapolation may bypass unresolved events.
4. Add local-client notification preferences and optional server push in invite-only multiplayer. Outbox messages are *not* operating-system push notifications at this stage.
5. Benchmark many independent settlements and interacting factories on consumer hardware; the trillion-person arithmetic test is not a whole-galaxy performance claim.

Architecture: one authoritative logical world, discrete events plus mathematical interval progression, detailed simulation only in active player contexts. No chits, player wages, real-money payouts or public matchmaking.