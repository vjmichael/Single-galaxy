# DEMOG-05q6e5b3a/b — Non-parent legal custodian and region-scoped dependent fences (v45b3)

**Date:** November 2, 2026. **Project:** original Single-Galaxy simulator (not EVEmu).
**Status:** Verified locally in a focused C++17/SQLite source package; NOT merged into the main playable game kernel, UI or Windows release. GitHub is repository/documentation, not the development host.

## 05q6e5b3a: new legal guardian

The host can now record *independent, immutable parental authorization receipts* for the whole dependent manifest from an already VERIFIED nonfatal rescue outcome. When two original parents are documented, BOTH must separately authorize the same child IDs and the same non-parent receiver. The receiver must be a real named adult physically present on the rescue ferry with the parents and children at each approval and legal settlement checkpoint. An unmatched parent, absent receiver, wrong manifest, duplicate or stale transfer is refused.

The host then commits one legal transfer for the entire child manifest: the two independently recorded approvals, authorization links, an immutable `npc_family_guardianship` record for every child, and world-metadata counters. Birth parentage, actual ferry history and resident headcount never change. `NpcAppearanceStore::current_nonparent_guardian` reads the current legally designated non-parent. This is an additive legal record, not a modification of historical physical boarding guardianship.

Every receipt is verified at database reopen and independent genesis recovery against the rescue outcome, actual family lineage, pinned historical world snapshots, parent/receiver physical ferry positions, child berths, and world journal. Interrupted transactions roll back completely, allowing safe retry. Offline deletion of approvals or a family move, forged recipient, altered ledger counter, and missing historical snapshots are rejected. No claim of external tamperproof cryptographic sealing.

**Consent limitation:** This milestone records decisions **attested by the trusted host**, not independently generated or cryptographically signed NPC consent, court orders, appeals or player interfaces. Explicit legal transfer of physical child custody from birth parent to unrelated escorted adult, parents leaving the ferry, temporary/non-consensual emergency care and later reassignment remain separate safety gates.

## 05q6e5b3b: region-scope cold/detailed transitions

A family's *first* region-declared child boarding commits an immutable family→region binding with real boarding command and generation, anchored in world metadata. An old active voyage cannot acquire a region designation retroactively. Both `prepare` and `commit` under `BEGIN IMMEDIATE` now fence only the target region's active child berths, and **fail closed globally** when any active legacy dependent lacks a trustworthy region binding. Rescue requires the same original family-region scope.

An actual persisted ownership-record test maintains two children aboard in Region A: A refuses promotion; unrelated Region B completes Cold→Detailed→Cold with a durable owner-generation rotation; A's physical ferry custody survives restart. Deleted region-binding tables fail recovery. This is NOT full warm/detailed gameplay, production, workforce or regional checkpoint effect reconciliation.

## Verified local evidence

| Check | Result |
|---|---|
| Strict GCC C++17 Debug | **30/30 passed** |
| Strict GCC C++17 Release (-O1) | **30/30 passed** |
| GCC Debug UndefinedBehaviorSanitizer | **30/30 passed** |
| Independent final source ZIP extraction, Release rebuild and CTest | **30/30 passed** |
| ZIP per-file SHA-256 manifest after final packaging | **217/217 matched** |
| v45b2→v45b3 source patch `patch --dry-run -p1` | **Passed** |

New tests: `demog05q6e5b3a_nonparent_guardian` (three real adults, two real dependent children, actual ferry rescue, dual-authorized legal custodian, rollback, restart, genesis replay, adversarial corruption) and `demog05q6e5b3b_region_scope` (independent regional owner rotation while another region contains active children, conservative local fence, tampered binding rejection). The 28 inherited test targets remain green.

**Artifact:** `SingleGalaxy_DEMOG05q6e5b3ab_NonParent_Region_v45b3.zip` (652,770 bytes), full focused source, v45b2-to-v45b3 patch, 30 tests, engineering report, Debug/Release/UBSan/clean build logs and per-file checksums.
**ZIP SHA-256:** `ef4ea70e28d308c319a3721441d60abb6dc117020ab7b3177536eb54766deb5e`.

## Explicitly open

- True mixed-family/split rescue manifests and one atomic rescue command covering multiple family scopes are NOT supported; the existing writer refuses an incomplete one-family manifest instead of leaving children behind.
- Actual non-parent *physical* custody/escort transfer, host-adjudicated consent sourced from NPC activity, reauthorization, temporary guardianship and legal oversight are not yet implemented.
- Detailed-world workforce, factory, regional cold/warm state checkpoint reconciliation and full-game integration remain open.
- Fatal child-bearing ferry incidents remain blocked, and **children must never be visually depicted being harmed or dying**. Full police, fire and institutional EMS remain deferred by user direction.

**Next:** complete mixed-family ferry rescue with a canonical multi-scope manifest, per-family generation/owner fences, composite command/member receipts and exhaustive rollback/replay; then physical guardian handoff and real detailed-world promotion/demotion reconciliation.