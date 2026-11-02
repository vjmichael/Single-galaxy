# DEMOG-05q6b — Atomic World/Family/Regional Society Settlement (v40b)

**Date:** November 2, 2026  
**Project:** Original Single-Galaxy simulator, NOT EVEmu server  
**Baseline:** Locally verified DEMOG-05q6 v40 medical society reconciliation  
**State:** **Implemented and verified in a local focused C++17/SQLite source package. NOT merged into the main game kernel or native Windows client.** GitHub serves only as source/documentation history.

## Authoritative feature increment

- Introduced a *host-only* `PersistentWorld::step_with_atomic_society` with an explicit `SocietyAtomicPlan` for **exactly one scheduled terminal ferry-loss command**. It inspects the simulated *execution result*, not only the prior command submission, and refuses missing, extra or duplicated casualty IDs.
- Commits the world step/snapshot, actual immutable ferry casualty tombstones, ART02 family death/census receipts, ECON03 two-colony checkpoint revisions and regional ownership receipt in **one SQLite transaction**. Failed writes roll back the whole operation before publishing in-memory world state.
- Reuses ART02's borrowed-transaction `NpcAppearanceStore`; its census-revision CAS and immutable family-person records remain authoritative. No alternate NPC registry or unearned population creation.
- Uses the actual ECON03 `CheckpointStore` scheduler codec through a host transaction API. Requires a real enrolled **stable Cold-owner generation**, matching expected checkpoint revision and bound SQLite file; prepared handoffs, stale owner token and detached DB fail closed. The scheduler's population events, not an ad-hoc edited snapshot, execute the regional deductions.
- Runs existing DEMOG-05q6 world/family/region reconciliation for each named home settlement *before* the mutation and repeats checked snapshot population/revision conditions inside the transaction. Home-to-regional-colony mappings are unique.
- Allows an actual colony to reach **zero living population**. The earlier ART02 `current_population>=1` CHECK is migrated transactionally to `>=0`, preserving existing rows and census revisions rather than inventing residents.
- On restart, validates `sg_med_society_effect` against world operation_log step, applied command event, regional command receipt, and every newly deceased NPC's matching ART02 death row. Corrupt/missing matching records fail closed.

## Native local verification

- GCC 14.2 strict C++17 Release: **17/17 CTest passed**.
- GCC 14.2 Debug + UndefinedBehaviorSanitizer: **17/17 CTest passed**.
- Fresh clean extraction of final distributed source archive, new Release build: **17/17 CTest passed**.
- Source archive checksum manifest: **84/84 file hashes matched**.
- New `demog05q6b_atomic` test covers 20-person source settlement plus reserved dependent child, four-person ferry settlement and eight-person EMS ship, rescue of two passengers, one home-colony death plus four ferry-crew deaths, atomic zero-population ferry settlement, both family and regional population reductions, simulated post-effect/pre-COMMIT failure followed by same-command safe retry, stale owner/revision/manifest/colony binding refusal, independently reopened legacy-family schema migration, ordinary restart, independent genesis journal replay, and altered casualty effect receipt rejection.
- Historical v34–v40 regressions remain green.

**Local deliverables:** `SingleGalaxy_DEMOG05q6b_Atomic_Society_v40b.zip` (313,846 bytes, full focused source, patch, test logs, engineering report) and standalone SHA-256 file. ZIP SHA-256: `0729319543e28fae8e9755b4b0cfaa6d98f46580ed3b6dc93a7da447dd55d977`.

## Remaining integration gates

1. Atomic gate currently handles one host ferry-loss command per step only. Births, migration, simultaneous incidents, general social employment/kinship events and family rehoming still need an authoritative cross-domain protocol.
2. It requires all stores on **one SQLite file** and a stable **Cold** regional owner. It is *not* a production active warm/cold handoff, distributed transaction, regional industry reconciliation or native Windows verification.
3. A future historical evidence sealing policy is needed against a malicious offline actor deleting every cross-store receipt together. Current restore checks corroborate existing records and fail on mismatches, not an external immutable anchor.
4. The focused source code is **not merged** into GitHub main's full game. The documentation commit records the evidence and explicitly preserves that boundary.
5. Full police/fire/emergency dispatch and civil-institution EMS remain **deferred** by user decision.

**Next engineering gate:** expand the host-owned writer to opt-in authoritative nonfatal transfers and birth/kinship updates, then verify cold/warm ownership transitions across multiple colonies. Keep physics, resource use, crew death permanence and NPC identity under one authority.