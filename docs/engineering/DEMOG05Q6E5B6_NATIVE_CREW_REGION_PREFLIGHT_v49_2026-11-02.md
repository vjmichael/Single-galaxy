# DEMOG-05q6e5b6 — Operating-crew rescue and region-population reconciliation (v49)

**Date:** 2026-11-02. **Repository:** vjmichael/Single-galaxy, original game (not EVEmu).

**Status:** Implemented, fully regression tested and packaged in a standalone focused C++17/SQLite source bundle. **Not merged into the full game kernel, Windows client or warm/detailed industrial simulation.** GitHub is repository/documentation, not build host.

## A. Operating-crew custody conservation

v48's two-receiver atomic rescue covered four named passengers and two ART02 dependents, but omitted six source-ferry Ship-role operators. v49 adds an optional, versioned operating-crew partition to the same single HostSplitEvacuatePassengers world step. The host enumerates *all* original assigned crew, sorted by immutable NPC ID, requires each to be physically on the stranded ferry, and places every one on exactly one of two authenticated receiving voyages together with all manifested passengers/children. Capacity counts named adults, crew and dependent berths.

A single SQLite BEGIN IMMEDIATE transaction durably commits both receiving voyages, complete old/new child berths, six immutable operating-crew custody receipts, a receipt activation count, journal and world snapshot. Failure after first receiver writes or after snapshot rolls **everyone** back to the original ferry. Reopen/recover_copy independently ties crew receipts to the pinned historical allocation-intent snapshot and final applied command, the voyage chain and canonical world state. Removed, forged or missing crew receipts fail closed.

**Legal staffing is intentionally unchanged:** crew retain their original home ship, job and employment registry. Rescue changes only physical custody. They are unavailable for duty at the source while traveling; they do not become invented staff on receiving ships. Permanent crew rehoming and vacancy settlement are future authorized actions. A backward-compatible zero-partition preserves v48 action semantics.

## B. Shared-region family and population reconciliation

New read-only verify_regional_population checks a supplied complete set of disjoint ART02 family scopes, all named world NPC residents and birth-certified dependent identities, actual family census memberships, ECON03 regional colony population and checkpoint revision, current committed world generation, and Stable Cold regional owner generation. It rejects double-enrolled NPC IDs, missing named residents, invented dependents, false family or regional counts, stale generations and prepared ownership handoffs.

The native test uses two true disjoint family registries with three living NPCs each sharing a production region, confirming exactly six residents. For rescued source crew, **registered population remains six while all six are physically away**, preventing erroneous workforce availability. A persisted Cold→Detailed→Cold **ownership-record** transition is tested with valid census counts and restart; a strict physical-presence check refuses transition when away workers are required.

Because native ferry crew currently lack an independently bound regional staffing authority in the focused kernel, region-mode transitions conservatively refuse while any rescued assigned operator is physically absent. This is deliberately stricter than full region-specific handoff and avoids false authorization.

## Reproducible evidence

| Verification | Result |
|---|---|
| Strict GCC C++17 Debug suite | **36/36 passed** |
| GCC C++17 Release (-O1) suite | **36/36 passed** |
| GCC Debug UndefinedBehaviorSanitizer suite | **36/36 passed** |
| Independent clean ZIP extraction, configure, Release build, full CTest | **36/36 passed** |
| Final source ZIP per-file SHA-256 manifest | **260/260 matched** |
| v48→v49 source patch dry run against pristine v48 | **Passed** |
| Failed receiver commit/snapshot, retry, genesis replay, forged/missing crew ledger and regional population corruption | **Passed/refused as designed** |

**Artifact:** `SingleGalaxy_DEMOG05q6e5b6_Crew_Region_Preflight_v49.zip` (797,777 bytes); SHA-256 `b5a94ec0a5cfe84b87c75f0dce91fe332b181cca17476c1636da11254cf84ba3`. Includes complete focused CMake source, 36 native test targets, v48→v49 patch, engineering report, staging/rollback/clean-build logs, warm-runtime handoff requirements and 260-source-file integrity manifest.

## Explicitly unfinished

**Actual detailed/warm production runtime integration is not implemented.** The focused package does not contain full production warm factories, resource inventory, workforce duty simulation or durable cold/detailed effect replay. A trustworthy full handoff must stage worker and industrial checkpoints, reconcile physical quantities and staffing, apply versioned region owner CAS/fencing, recover from mid-handoff crashes, and prove correct cold catch-up and demotion. An ownership-record toggle alone is not acceptance; see the packaged `WARM_INDUSTRY_PRODUCTION_INTEGRATION_GATES_2026-11-02.md`.

Full game main-source merge, native Windows verification, player interface and externally sealed provenance remain open. Child-bearing fatality incidents remain blocked; **never visually depict children harmed or dying**. Police/fire/full EMS institutions remain deferred as requested.

**Prior status overview:** `SINGLE_GALAXY_ENGINEERING_STATUS_v34_v48_2026-11-02.md`; this v49 milestone extends its ledger.

**Next:** Durable rescued-crew staffing/region authority binding and full warm/detailed workforce+industry promotion/demotion reconciliation, then final game integration.
