# DEMOG-04a — Durable pioneer expedition escrow (native SQLite prototype)

**Recorded:** November 2, 2026  
**Status:** Native C++17 exploratory integration gate complete and locally verified. **Not yet connected to production PersistentWorld or launched by NPC automation.**

## What was completed

The existing DEMOG-03 read-only pioneer planner has been extended locally with three new source files and a CMake registration:

- `include/galaxy/PioneerExpeditionLedger.hpp`
- `src/PioneerExpeditionLedger.cpp`
- `tests/DEMOG04PioneerEscrowTests.cpp`

These implement an isolated SQLite-backed pioneer custody model with stable command identity, host policy checks, optimistic revision, physical manifest and ship/site reservation, provision escrow, departure, transit, landing, cancellation, and ship destruction. Each operation persists its receipt and state in the same `BEGIN IMMEDIATE` / COMMIT transaction. A fault injection immediately before commit demonstrates SQLite rollback.

### Verified 500-person scenario

A physical colony seeded *for the fixture* with 500 residents (240 adults and 260 dependents), one eligible transport (120 capacity), and actual local food/water/oxygen/construction stocks dispatches 100 travelers (70 adults and 30 dependents). A successful reserve moves 100 out of the settled cohort and escrows 200 food, 200 water, 200 oxygen and 650 construction material units. Departure at tick 5 arrives at tick 6. A completed voyage consumes 100 each food/water/oxygen. The destination then contains 100 real accounted cohort residents and exactly 100 remaining food/water/oxygen and 650 construction material. The source retains 400 people. Nothing appears for free.

The implementation refuses unclaimed/unsurveyed/unsafe/undersized sites, a missing ship, insufficient adults, insufficient rations, source emergency-reserve violations, stale revisions, mismatched receipts, unauthorized host commands, and landing before ETA. A cancellation restores reserves and people; a destroyed transport permanently accounts for lost people/cargo and cannot arrive.

## Tests performed

- **7/7 focused/regression native CTests passed:** DEMOG-04 expedition, DEMOG-03 planner, core kernel, core persistence, ECON-03c4 regional durability, ECON-03c5 host commands, ECON-03c8 real-world bridge.
- **250 sequential reserve/cancel cycles** verified exact stock/headcount restoration.
- Local C++ ASan + UBSan run with leak detection passed.
- Combined DEMOG-03 + DEMOG-04 source patch dry-run against fresh local ECON-03c10 native baseline passed.
- A full 95-target build reached approximately 87% before tool execution timeout. The subsequent attempt to run all 95 tests was invalid because many test binaries had not been built. **No full-suite pass is claimed.** There is no evidence those unbuilt tests failed at runtime.

### Exact delivered evidence

Conversation artifact: `SingleGalaxy_DEMOG04_Durable_Expedition_Escrow_Source_Tests_2026-11-02.zip`  
SHA256: `d9d2200cc9457a5fbd8907aafa089b4bf3e4e4f4c3c753d77c82f1366087d7a5`

The artifact contains the full local source tree snapshot excluding compiled binaries, DEMOG04-only and combined DEMOG03+04 CMake/source patches, direct reproduction instructions, CTest logs, sanitizer log, and full engineering report.

## Critical trust boundary / what is NOT done

This is **the authoritative transactional source of truth within its own isolated prototype ledger only**. It is **not yet authoritative against the game's real `PersistentWorld`**, `RegionalModeAuthority`, actual manufactured ship inventories, ART-02b1 named/family NPC records, or real planetary construction. Stage transitions are explicitly invoked by an authorized host; there is no autonomous travel timer or player-facing launch. Bootstrap colony/site/ship functions exist for privileged integration fixtures, not untrusted client actions. Host policy only has security value if the owner authenticates actors independently of caller-supplied strings.

The test models passenger groups in adult/dependent cohorts and records actual material quantities, but does not yet reconcile individual crew IDs, appearance genomes, family relationships, occupations in the complete world ledger, independent food/ore production, risk and death during a voyage, or cold/warm custody handoff.

## Next gate: DEMOG-04b

Connect the pioneer ledger transaction to the **actual world-owned** source/destination population, real ship entity, inventoried physical cargo, unique named NPC identity records, family metadata, and cold/warm region ownership. Avoid adding two competing population/stock authorities. Add real world effect receipts and cross-region save/restart/fault injection tests, then enable host-delegated NPC launches only after those are green.

Related design: [NPC pioneer diaspora](../design/NPC_PIONEER_DIASPORA_COLONY_EXPANSION_2026-11-01.md). Previous engineering: [DEMOG-03 advisor](DEMOG03_NATIVE_NPC_PIONEER_PLANNER_2026-11-01.md).
