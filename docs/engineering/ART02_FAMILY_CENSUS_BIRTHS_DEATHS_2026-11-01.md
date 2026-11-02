# Single Galaxy — ART-02: Durable family births, deaths and facial inheritance

**Date:** November 1, 2026  
**Status:** Local source implementation and focused verification. **Not a playable-world birth implementation**; newborns do not enter `PersistentWorld::Simulation::npc_records()` until the next migration gate.

## Why this layer exists

ART-01 generated genetically related facial parameters but had no population effect, family birth ledger or family-life events. ART-02 introduces a host-authorized, opt-in **named-family census** per colony, in the same SQLite database and transactional connection as the appearance profile. The population baseline must be supplied by a trusted host population authority. A million-person colony does **not** receive a million face records; only individually admitted founders and named children do.

## Data and implementation

- `NpcAppearanceStore::register_settlement(settlement, population)` records an immutable initial population baseline. This does not alter the playable `Simulation` or the macro `RegionalColdIndustry` census.
- `enroll_family_founder(npc_id, settlement)` verifies an existing `Simulation::npc_records()` identity via a trusted admission callback; it **does not increment population**. A founder cannot be secretly assigned to multiple settlements.
- `record_birth(event_id, child_id, parent_a, parent_b, settlement, tick, expected_revision, authorize_birth)` checks immutable, same-settlement, persistent parentage; rejects collisions with playable NPC IDs; uses reserved `child_id >= 2^62`, *not* playable NPC IDs. The host callback is mandatory for housing, resources, population policy and age eligibility. It atomically appends a durable birth, creates the individual's inherited 160-byte facial genotype, and increments the cohort census **once**. Replaying the identical event returns the original record without incrementing the population. Stale revision, earlier tick, duplicate child, contradictory replay, missing parent, same parent twice and denied host authorization fail closed.
- `record_death(...)` similarly decrements census once, appends an immutable death event, prevents duplicate deaths, prevents future childbearing from the deceased identity, and keeps the historical face and family lineage accessible.
- Both birth and death events use `BEGIN IMMEDIATE` / `COMMIT`, and the census, profile and journal share a single SQLite database connection. The next birth/death for a settlement must have the current revision. Journal rows are append-only with SQLite triggers. The metadata schema and appearance seed are scope-bound.
- A child born at tick T cannot be used as a parent in an event at tick T or earlier; **actual maturity-age thresholds** need a future game calendar and the host's authorization policy.

## Verification — local Linux

- Focused Release CTest: `galaxy_npc_appearance_tests` and `galaxy_npc_family_birth_tests` **2/2 PASS**.
- Adjacent native tests: `galaxy_persistence_tests`, `galaxy_b70n_workforce_tests`, plus two above **4/4 PASS**.
- ART-02 targeted ASan+UBSan (family/appearance sources instrumented, linked to existing Release `galaxy_kernel`) **PASS**. Full-world dependencies are not sanitizer-instrumented.
- 100 synthetic family lines: 300 named founders, 300 named births (two children and one grandchild each); family-persistence/restart consistency; one journaled death. Starting population 1,000,000, end 1,000,299. Parent-child mean facial-parameter distance ~0.1357 vs unrelated ~0.2461. This numerical metric **does not** establish visual realism.
- Actual `PersistentWorld` founder admission: existing 24 crew IDs enrolled. Named child born in family census and face survives SQLite restart, while actual `Simulation::npc_records()` correctly remains 24 — **deliberate integration boundary**, not an extra playable person.
- Forced SQLite failure during birth journal insert: restored population, profile and event counts; subsequent retry succeeded.
- Rejection tests: unauthorized birth/death, stale revision, replay mismatch, reusing child identity, wrong founder, bad parent scope, dead parent, repeated death, and SQL attempt to delete immutable events.
- Full 95-test native build was **not completed**: all-target compile was interrupted by execution limit at ~60/178 build steps. Do not claim the whole-project regression has passed. No Windows or graphical tests were run.

## Next gates

1. **ART-02b: actual world birth and death** — record in `PersistentWorld`'s own operation/effect journal; update ship/habitat headcounts, food and housing requirements, named roster, census, and appearance in one authoritative transaction with restart recovery. Version the `Simulation` snapshot schema and roundtrip old saves safely; do not copy an external birth-count ledger on top of the existing population.
2. **ART-02c: age/care/households** — persistent age, life-stage progression, newborn caregiving, mortality, migration, parental custody and eligibility policy calibrated to a game calendar. Resolve when an aggregate child is promoted to an individually rendered NPC.
3. **ART-03/04:** procedural mesh/face morph preview for three generations; rig eyeballs/eyelids/jaw/visemes; evaluate family resemblance visually, not only statistically.
4. Warm/cold: reconcile immutable family event receipts with both cold and detailed region owners; reject duplicate births/deaths after ownership transitions.

**Implementation is opt-in and not a player-exposed birth mechanic.** No claim is made that 1,000,299 persons are now inside a real playable colony; the independent family census must be migrated to the actual population authority before gameplay use.
