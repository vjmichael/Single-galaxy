# DEMOG-05q6e5b5b — Atomic two-receiver ferry rescue (v48)

**Date:** 2026-11-02. **Project:** Original Single-Galaxy C++17 / SQLite simulation (not EVEmu).
**Baseline:** Locally verified v47a split-allocation intent.
**Status:** Implemented and verified in local focused source. **Not merged into the full playable game kernel, UI or native Windows build.** GitHub is a repository and engineering record, not the development host.

## Verified result

The host authenticates one immutable v47a split-rescue intent for a stranded passenger ferry, then applies ONE new canonical `HostSplitEvacuatePassengers` world command. It closes the original manifested passenger voyage and opens two distinct receiving voyages in the SAME simulation step. Both adult passenger subsets are sorted/disjoint; each named adult keeps one original NPC ID. Every manifested ART02 dependent berth closes on the original ferry and opens precisely once on its assigned receiving ferry in one SQL `BEGIN IMMEDIATE` transaction. The new in-memory world is published only after commit.

An original ferry containing two ART02 families (four named adult parents and two dependent children) is rescued onto two real receiving ferries, each carrying two adults and one child. Exact frozen manifest, original and proposed transport IDs, dual receiver identities, real berth capacity, parentage, regional stable-Cold owner tokens and source transport status are verified. The host refuses ordinary `PersistentWorld::step()` execution of the split action to prevent a world-only, child-free bypass.

New v48 append-only command encoding (`S48` extension to T31) and versioned snapshot field preserve earlier action ordinals and journal formats. The split-execution receipt is immutable and linked to the allocation intent, actual applied command and historical generation. Original and new snapshots remain pinned as required. On reopen and independent genesis recovery, the kernel checks both child travel legs, receiving ferry/voyage IDs, world journal, complete family-batch receipts and world metadata counter. The number of applied split commands must match the execution receipts **bidirectionally**, so deleting a receipt and its counter together is rejected.

## Injected crash outcomes

Failure after the FIRST receiver's child records or after the new world snapshot rolls the entire SQLite transaction back. Following reopen, both families are still aboard their stranded original ferry. Retrying commits both receivers together. Successfully rescued adults/dependents have exactly one receiving ferry and history; no duplicate living NPC or child identity is created. Corrupted guardian/transport destination, missing child receipt and forged execution evidence are refused.

## Reproducible local verification

| Check | Result |
|---|---|
| Strict GCC C++17 Release full CTest | **34/34 pass** (33 inherited) |
| Debug UndefinedBehaviorSanitizer full CTest | **34/34 pass** |
| Independent source copy, fresh Release configure/build/full CTest | **34/34 pass** |
| Patch v47a→v48 dry-run on pristine baseline, apply and byte-compare changed files | **Pass** |
| Final ZIP extraction SHA-256SUMS manifest | **252/252 matched** |

**Deliverable:** `SingleGalaxy_DEMOG05q6e5b5b_Atomic_TwoFerry_v48.zip`, 751,449 bytes. Contains complete focused CMake/C++17/SQLite source, all 34 native tests, the applicable v47a→v48 patch, engineering report, Debug sanitizer/Release/clean-build logs and full SHA-256 manifest.
**ZIP SHA-256:** `14e1303f32713161e6ee1128a7b87c9fb5c0fb35419df978681076f3affdfa3f`.

## Important limits

- **This rescues the entire authenticated PASSENGER manifest, not the stranded vessel's native operating crew.** The source ship's separately assigned crew remains on board because it is not a manifested passenger. A future crew evacuation protocol must account for those NPCs; do not claim total-vessel abandonment.
- The successful rescue uses one source and two physically co-located receiving ferries, distinct voyage IDs, both families complete, 16 berth limit per receiver, one SQLite authority and stable cold regional owners.
- Full real-world shared-region ART02/ECON03 population consolidation, promotion/demotion of detailed warm workforce/industrial runtime, splitting an individual family across receiving ferries, rescue of unmanifested crew, full client integration and native Windows acceptance remain open.
- The deterministic intent checksum is a consistency crosscheck, NOT cryptographic external tamper sealing.
- Dependent fatalities remain blocked. **Never visually depict children being harmed or dying.** Full police, fire and institutional EMS remain deferred.

This implementation **supersedes** the unimplemented status in the earlier `DEMOG05Q6E5B5B_ATOMIC_SPLIT_EXECUTION_ACCEPTANCE_2026-11-02.md` acceptance contract for the specific passenger-manifest two-receiver gate. The broader full crew evacuation is not superseded.

**Next:** exact native-crew rescue/custody conservation, production shared-region population integration, and full warm/detailed checkpoint worker/industry reconciliation.