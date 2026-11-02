# DEMOG-05q6e4 — Two-parent legal household migration (v44)

**Date:** 2026-11-02
**Project:** Original Single-Galaxy simulation, not EVEmu server.
**Baseline:** Locally verified DEMOG-05q6e3a/b focused C++17/SQLite v43 source.
**Status:** Implemented and locally verified in a separate focused v44 source bundle. This document is NOT a merge into the main game source, Windows build, or full client. GitHub remains the source/documentation repository, not the development hub.

## New authoritative behavior

- Opt-in paired adult HostSettleMigrant command, with sorted real NPC IDs and a shared, completed physical ferry voyage. Two adult home_ship, assigned_site, and colony crew counts are changed in one world step, rather than sequential unguarded moves.
- SocietyMigrationPlan carries the second parent's identity and an independent immutable family move event. Incomplete/wrong second parent, reversed IDs, duplicate event, missing shared arrival, or incompatible crew assignment fails closed.
- ART02 records both adults and every declared dependent's registered residence under the same SQLite transaction. Birth/lineage identities remain unchanged. The child identities remain census dependents, never fabricated workers.
- ECON03 applies one -N/+N regional population settlement under stable Cold ownership with expected generation and checkpoint revision. Regional receipt, ART02 family receipts, world snapshot and canonical command journal commit together or roll back together.
- The cross-store society receipt adds backwards-compatible co_parent_id and co_parent_event columns. Reopening verifies both adults' pre/post world snapshots, family event identities, region delta, ordered child links and world operation journal. An adversarially changed co-parent event is rejected.
- New snapshot marker galaxy-kernel-v44 for paired settlement; older one-adult migration and snapshot versions remain accepted.

## Test evidence

- Strict GCC 14.2 C++17 Release CTest: **25/25 passed** (24 inherited plus the new paired settlement suite).
- Debug UndefinedBehaviorSanitizer CTest: **25/25 passed**.
- Independent extraction of the source ZIP, fresh Release compilation, and new targeted test: **1/1 passed**.
- Final source archive: **160/160 per-file SHA-256 checks passed**; artifact checksum 88a8a02abe5627b343e25a30282d9f2ae3bcfee8eef00d9cf8a095ccb181ef49.
- Concrete test: two real parents and two physically boarded dependents complete the ferry journey, then legally migrate as one household. Registered home population origin **8→4**, destination **6→10**, combined **14 unchanged**; real adult colony crew source **6→4**, destination **6→8**. Safe rollback/retry, cold→warm owner freeze/abort and generation rotation, restart, and forged co-parent history rejection pass.

## Limits and next tasks

- Child-bearing ferry destruction/mixed fatalities remain blocked until a complete family/casualty/guardian/census protocol is implemented, without any depiction or visualization of children being harmed or dying. See DEPENDENT_INCIDENT_NON_GRAPHIC_POLICY_2026-11-02.md.
- No production warm-region workforce/industry promotion/demotion integration, only stable Cold ownership plus tested prepared-handoff fencing.
- No native Windows acceptance, whole-game merger, client/UI integration, or full public police/fire/EMS institutions; institutional emergency services remain deferred by user direction.
- Full child guardianship, split itineraries, multi-region concurrent household migration, and family rehoming remain future work.

**Local deliverable:** SingleGalaxy_DEMOG05q6e4_TwoParent_Household_v44.zip, complete focused source, v43→v44 patch, engineering/policy docs, Release/UBSan and clean extraction evidence.