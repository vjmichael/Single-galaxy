# DEMOG-05q6e2 — Physical dependent ferry custody (v42)

**Date:** November 2, 2026
**Project:** Original Single-Galaxy C++17/SQLite simulation; not EVEmu server.
**Baseline:** DEMOG-05q6e1 locally verified v41e focused kernel.
**Status:** Implemented, locally tested and packaged, NOT merged into the full game main kernel, playable UI or Windows release. GitHub is the engineering repository, not the build host.

## What changed

1. Host-only DependentTravelPlan executes exactly one real, scheduled HostEmbarkPassengers or HostDisembarkPassengers command. The actual guardian NPC, physical ferry, origin/destination, voyage and expected world generation are authenticated.
2. The ART02 child manifest contains sorted, unique, real high-range family census IDs (up to 15). Parentage and current home are verified. An incomplete dependent manifest, duplicate child or concurrent boarding is rejected.
3. Child passengers occupy genuine ferry berths, with the same total 16-passenger physical limit as adults. While aboard, their actual system and travel progress derive from the committed ferry entity. They are not spawned as separate worker NPCs.
4. Child travel and batch receipts are committed in the same SQLite world transaction as the physical boarding/disembarkation step. Status 1=aboard, 2=arrived. A failure leaves all parties in last committed custody. Boarding/arrival command IDs, logical ticks and world generations are durable.
5. Ordinary world step refuses to complete a dependent-bearing disembarkation without the authorized child effect. Host ferry destruction/evacuation is blocked while a ferry has active children, pending explicit child incident and rescue semantics.
6. New household immigration is refused without completed child arrival evidence on the guardian's actual voyage. The new physical_manifest=1 migration receipt is checked on restore, including missing tables and downgraded attestations. Older v41e residence-only migrations remain readable with physical_manifest=0 and are not falsely represented as real child journeys.
7. SQLite restore verifies the actual boarding and arrival commands in the operation journal, the child birth/guardian, corresponding world-step generation, complete batch/berth counts, physical ferry status, and migration links. Corrupted or absent records fail closed.

## Local verification

| Verification | Result |
|---|---|
| Strict GCC C++17 Debug | 22/22 CTest passed |
| Strict GCC C++17 Release (-O1) | 22/22 CTest passed |
| GCC Debug UndefinedBehaviorSanitizer | 22/22 CTest passed |
| Source ZIP internal SHA-256 manifest | 135/135 matched |
| Fresh ZIP extraction, independent Release build and new test | 1/1 passed |

The new demog05q6e2_physical_dependents test uses Bob and two real ART02 children: injected failed boarding commit, restart with no leaked boarding, actual committed boarding and occupied berths, failure during ferry jump progress, actual inter-system travel, refusal to bypass non-atomic child disembarkation, failed arrival commit followed by safe retry, original home until legal migration, atomic registered household move, historical world replay, forged arrival, missing batch, deleted travel table, and downgraded physical certification. All 21 inherited native tests passed.

## Package

SingleGalaxy_DEMOG05q6e2_Physical_Child_Custody_v42.zip — 436,272 bytes. Includes complete focused source, v41e-to-v42 patch, CMake, 22 tests, report, Release/Debug/UBSan logs, and clean archive verification evidence.
SHA-256: d6462444c4f017a74d7640e890118ae0227a525fbed2997a01fee7930ce52afa

## Outstanding gates

- No two-parent household ferry migration, independent playable child NPC agent, or parental separation/guardianship policy yet. Dependents are physical named cohort passengers, but not spawned adult worker entities.
- Full dependent ferry evacuation, medical triage, mixed survival/casualties and ferry loss are intentionally blocked until explicit child-specific incident/custody rules exist.
- The child ledger shares the world SQLite transaction and verifies against world snapshots/journals on restore, but it is not yet embedded inside the Simulation::canonical_state blob or externally cryptographically sealed.
- Requires one SQLite file and stable Cold regional owner for final legal migration; active warm/cold handoff, full production client, main kernel merge and native Windows validation remain open.
- Full police/fire/public safety dispatch and institutional EMS remain deferred as directed.

**Next:** 05q6e3 paired-parent passenger and whole-household migration, dependent rescue/loss contracts, and cold/warm authority transition tests.