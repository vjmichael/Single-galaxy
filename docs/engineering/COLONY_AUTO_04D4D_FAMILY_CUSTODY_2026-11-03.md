# COLONY-AUTO-04D.4D1 — Verified Family / Dependent Departure Preparation

Date: 2026-11-03. Branch: `integration/colony-auto04d4d-family-custody-v73-20261103`. Base: AUTO-04D.4C v72, 12/12 focused native tests passing.

## Critical identity boundary

The original Single-Galaxy engine does **not** store ART02 dependent children as native `Simulation::NpcRecord` workers. A child is a reserved high-range ART02 family census identity with a separately tracked birth, parent(s), current physical/legal residence and, potentially, adjudicated non-parent guardianship. Treating a child as an ordinary `HostColonyDeparture` passenger would create fictitious workers, break guardian custody and conceal the true shipboard food/water/oxygen load.

The v72 adult-only safety boundary stays in force. **No child is moved, created, killed or pronounced rescued by the v73 preparation API.**

## Completed 04D.4D1 native foundation

`colony_family::assess` consumes the authoritative world `Simulation`, the **same live SQLite database** as the single-world writer, a family scope/seed, the exact sorted living adult manifest, and the exact sorted child-to-guardian roster. It fail-closes unless:

- All living named source adults (ship crew, farm workers, habitat residents) are present exactly once, have compatible physical/assigned locations, and reconcile with existing aggregate workforce counts.
- Every source ART02 child is recorded in `npc_family_births` with a matching independent **`sg_med_society_birth`** command/tick/source-family birth bridge, no ID collision with real living NPCs and a current residence at the colony.
- **Every** birth linked to the source is examined **across all family scopes**; an unlisted second family cannot become an invisible abandoned dependent.
- Each child accompanies an actual currently living recorded parent or verified legal non-parent guardian, included in the adult manifest. This first conservative adapter requires both original recorded parents to be present as well; more complex lawful custody with deceased or absent parents is a separate adapter.
- The complete ART02 source census equals the named adults plus accounted dependents, and receiving census equals existing receiving native workforce; no unmatched residents or ghost children can be hidden.
- The source ship and occupied source worksites are within the native docking range of the same-owner receiver, with no pre-existing passenger voyage custody. The incoming adults plus children fit within **16 actual passenger berths**. The real same-owner receiving vessel holds at least two consumption periods of **food, water and oxygen for everyone already aboard plus ALL incoming adults AND children**. This is stricter than the v72 adult-only budget; it is a preflight, not an actual reservation.

`PersistentWorld::audit_colony_family_departure` exposes the native read-only proof. The returned `ready_for_transaction=true` means **evidence is complete enough to begin designing an integrated commit**; `evacuated` is always false.

`PersistentWorld::prepare_host_colony_family_departure` takes an intent ID and exact expected world generation. Inside `BEGIN IMMEDIATE`, it rechecks the world generation and all ART02 evidence, then commits an immutable `sg_colony_family_departure_prepare` record with source, receiver, full adult roster, child:guardian roster, family scope/seed and FNV world-state hash. A repeated identical preparation is idempotent; stale or changed intent reuse is refused. Injected failure before commit rolls back entirely. **This is not a rescue execution or an ART02/ECON03 population migration receipt.**

The FNV witness is a deterministic integrity identifier, **not cryptographic protection** against privileged, adversarial SQLite modification. The original birth/guardian authority must remain trusted.

## Native tests

`COLONY_AUTO04D4D_FamilyProofTests.cpp` builds real named ship crews, registers ART02 founding population, records a genuine ART02 reserved-ID child birth, mirrors the world-birth bridge as a controlled fixture, and verifies complete adult/child closure. It checks missing/forged/duplicate children, wrong guardian, incomplete adults, unlisted second family scope, corrupted independent birth bridge, immutable receipt trigger, crash-rollback, duplicate prepared manifest idempotence, reused/stale intent refusal and explicitly **no rescued children**. The synthetic birth bridge fixture is deliberately distinguished from a production regional birth transaction.

`.github/workflows/colony-auto04d4d.yml` builds and runs this proof alongside v72 actual adult-departure and life-support acceptance.

## Necessary 04D.4D2 execution work (still open)

1. Introduce a versioned **simulation physical dependent occupancy** ledger and consumption accounting, with strict replay validation and independent proof of each child's identity (not a generic extra crew number). Every child must consume food, water and oxygen at its *actual location* before and after transfer.
2. Use canonical world command plus ART02 `record_guardian_household_moves`, ECON03 `commit_migration_in_world_transaction` and guardianship authority inside **one SQLite generation transaction**. Maintain full source/receiver census CAS, cold/detailed population mode ownership and family revision proofs.
3. Bind an actual adult+child arrival and a new immutable executed rescue receipt to the prepared proof. Reject split households, duplicate child custody, missing or deceased guardians, cross-owner transfers, insufficient resources, failure between any two writes, and stale live-world generation.
4. Only then assert the source is evacuated and allow the v58 abandoned-hull registry to close the complete population—including children. Re-test database restart, independent journal recovery, both hot and cold simulation and Windows.

**Status:** v73 D1 implementation committed; complete atomic family/dependent evacuation is NOT yet accepted.
