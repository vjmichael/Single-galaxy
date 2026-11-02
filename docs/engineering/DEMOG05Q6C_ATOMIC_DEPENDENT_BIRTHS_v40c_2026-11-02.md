# DEMOG-05q6c — Atomic ART02 Births and Regional Population Fences (v40c)

**Date:** 2026-11-02  
**Project:** Single-Galaxy original C++17/SQLite simulator; not the EVEmu server.  
**Baseline:** locally verified DEMOG-05q6b v40b.  
**Status:** **Implemented and independently verified in a local focused source package. Not merged into GitHub main's playable game kernel or Windows release.** GitHub is repository/documentation, not build host.

## Previously documented

- Passenger/medical milestones v34–v39 and deferral of full police/fire/EMS are recorded under `DEMOG05_CIVIL_INSTITUTIONS_HANDOFF_2026-11-02.md`.
- v40a read-only ART02/ECON03/medical population reconciliation and opt-in healthy workforce selection are recorded under `DEMOG05Q6_MEDICAL_SOCIETY_v40_2026-11-02.md`.
- v40b atomic multi-colony ferry casualty settlement is recorded under `DEMOG05Q6B_ATOMIC_MEDICAL_SOCIETY_v40b_2026-11-02.md`. **No need to duplicate or claim a code merge.**

## This iteration: v40c implementation

1. New **host-only** `PersistentWorld::step_with_atomic_dependent_birth(SocietyBirthPlan)` executes a real, otherwise command-free world tick and commits ART02 family child identity/lineage/census and ECON03 regional population increment through **one** SQLite `Store::commit` transaction.
2. Both parents must be real surviving NPC IDs, physically present in their home settlement and registered with ART02; the child must have a new high-range ART02 dependent identity, not an actual `Simulation::NpcRecord`. Child births are **not free workers**.
3. `SocietyBirthPlan` binds expected world generation, family census revision, regional checkpoint revision, cold-owner generation/phase, scopes, home colony, child ID and immutable event ID. Prepared cold→warm handoffs, stale owner tokens, mismatched family registration, duplicate children and conflicting pending commands reject.
4. `CheckpointStore::commit_birth_in_world_transaction` uses the real ECON03 scheduler to schedule the +1 population change and records a regional receipt, rather than editing a checkpoint blob directly. Same physical SQLite database is required.
5. Reopen verifies the world journal's step, `sg_med_society_birth` effect row, independent regional `society-birth-host` receipt, ART02 birth event and child's family-person lineage. Missing or forged counterpart records fail closed. `AfterSnapshotWrite` injection rolls back the world, family and region together, permitting safe same-event retry.

## Local test evidence

| Test | Result |
|---|---|
| GCC strict C++17 Release CTest | **18/18 passed** |
| GCC Debug + UndefinedBehaviorSanitizer | **18/18 passed** |
| Final ZIP clean extraction, independent configure/build/CTest | **18/18 passed** |
| Final ZIP SHA-256 per-file manifest | **97/97 verified** |

The new `demog05q6c_atomic_birth` test covers 12 founding real NPCs and two distinct ART02 reserved children; family and regional populations become 12→13→14 while the playable NPC headcount stays 12. It refuses missing/non-enrolled parents, duplicate child identity, prepared regional handoff, stale owner generation, and stale world generation. It tests abort-based owner token rotation, simulated post-effect failure and safe retry, restarted world, independent genesis replay-copy, forged birth lineage, altered regional receipt, and deletion of a family birth after deliberately removing its offline immutability trigger. All 17 earlier native tests remain passing.

**Local package:** `SingleGalaxy_DEMOG05q6c_Atomic_Birth_v40c.zip` (334,965 bytes, full focused source, v40b→v40c patch, 18 native tests, local and clean-build logs, engineering report, SHA-256 manifest).  
**SHA-256:** `bf13f590c9692b37aa0a2cba180fd84c49a7e435a189af69bc7256166dc3a1cc`

## Explicit unfinished work

- This is a cohort/identity birth only: **no playable newborn, childcare AI, gestation, material consumption, physical habitation/food admission, population policy or fertility balancing**. Host demographic policy must authorize those separately.
- Adult/child **home-colony migration** still lacks canonical `NpcRecord.home_ship` transfers, family membership reassignment and paired regional origin/destination population changes. Birth is NOT a migration shortcut.
- This transaction requires one SQLite file and stable Cold owner; it rejects handoff phases. No actual warm detailed-runtime promotion/demotion, industrial checkpoint reconciliation or native Windows game acceptance is claimed.
- Existing world snapshots retain legacy v40 semantics. This milestone adds a host API and cross-store receipt, not a silent rewrite of historical worlds.
- **Full police/fire/EMS dispatch institutions remain deferred** as requested.
- A full-source GitHub main merge is not represented by this documentation commit. Host/provenance validation is not an external cryptographic seal against a database editor who rewrites every related ledger.

**Next proposed gate (DEMOG-05q6d):** real finite nonfatal NPC/household migration: trusted ferry arrival + canonical source/destination home/assignment updates, family census and dependent membership handling, equal/opposite regional population events, idempotent authorization, and restart/replay/cold–warm fencing.