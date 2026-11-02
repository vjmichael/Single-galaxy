# Single Galaxy — NPC Facial Inheritance ART-01 Engineering Report

**Date:** 2026-11-01
**Baseline:** Local complete prototype recovered from `SingleGalaxy_ECON03c10_Warm_Region_Source_Tests_2026-11-01.zip`.
**Status:** Implemented/tested source for an opt-in facial-appearance registry. This is not a working 3D face renderer or birth simulation.

## Source changes

- `include/galaxy/NpcAppearanceGenome.hpp`: appearance-only allele data, 20 normalized facial phenotype traits, versioned persistence API.
- `src/NpcAppearanceGenome.cpp`: portable deterministic inheritance using SplitMix64 and fixed FNV scope hashing, bounded rare variation, SQLite-backed profile immutability, world/seed scope guard, checksum and atomic inserts.
- `tests/NpcAppearanceGenomeTests.cpp`: real `Simulation`/`PersistentWorld` admission, living ID checks, generation inheritance/restart/corruption tests, 100 synthetic families with 700 explicitly authorized named profile records and three-generation lineage; no unverified NPC births.
- `CMakeLists.txt`: `galaxy_npc_appearance` standalone library and CTest target.
- `docs/design/NPC_APPEARANCE_GENOME_V1_2026-11-01.md`: architecture and integration limits.

## Tests executed locally on Linux

- Release CTest `galaxy_npc_appearance_tests`: **1/1 PASS**.
- Release CTest `galaxy_persistence_tests`, `galaxy_b70n_workforce_tests`, `galaxy_npc_appearance_tests`: **3/3 PASS**.
- AddressSanitizer + UndefinedBehaviorSanitizer instrumented appearance source and tests (linked to release world kernel), **PASS**; does not instrument every dependency of `galaxy_kernel`.
- Test fixture: 24 real named `PersistentWorld` NPCs, enrolled as appearance-only founders with SQLite restart preserving digests, and negative checks for nonexistent/unauthorized IDs. Existing work tick does not change their faces.
- Synthetic family fixture: **100 families, 700 named appearance profiles, generations 0–2**; persisted and reloaded; sibling distinction; idempotent parent-order normalization; immutable parentage; mismatch-scope failure; corrupted allele BLOB rejected.
- Aggregate mean child-to-parent normalized feature distance: **0.134284**. Mean same-child to unrelated founder distance: **0.238623**. This demonstrates a numerical resemblance tendency; it is **not** a perceptual face-quality metric.

## Correctness boundaries

- Existing world NPC population remains the authoritative source; appearance registration does not create a person or consume food.
- The standalone appearance database table is not yet wired into the real birth/event journal or warm↔cold ownership-fence transactions. Multi-database/worker concurrency remains a next gate.
- No photoreal face texture generation, mouth rigging, eye tracking, aging model or 3D visuals have been executed in this milestone.
- The multi-generation test uses an explicit admission mock for child IDs because births are not yet supported in the playable `PersistentWorld`; do not claim the gameplay birth system exists.
- FNV64 is a corruption detector, **not** a tamperproof host authorization or digital signature.

## Next

Integrate admitted family/birth events atomically with the world/census ledger, then create a compact mesh/face-rig preview that consumes normalized morphs and performs gaze and viseme animation without changing identity across save/restart.