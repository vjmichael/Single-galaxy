# DEMOG-05q6e5b1 — Journaled dependent stranding cases (v45b1)

**Date:** 2026-11-02
**Repository:** vjmichael/Single-galaxy (original game, not EVEmu)
**Baseline:** Locally verified DEMOG-05q6e5a (v45a)
**Status:** **Implemented and verified as a focused local C++17/SQLite source package. Not merged into the main playable game kernel, desktop client or Windows build.** GitHub is for source/documentation, not the build host.

## New incident case

A trusted host can record an immutable nonfatal stranding case with an exact manifest of all actual adult ferry passengers and every dependent child berth, tied to the original voyage, current voyage, actual ferry, previously APPLIED HostStrandFerry command and observed world generation/tick. Children are individually bound to their recorded ART02 parent/guardian IDs and physical boarding generation. Invalid, duplicate, incomplete or cross-family manifests reject. One case represents a single verifiable incident, not merely an uncorroborated report.

**Disposition 1 means KNOWN STRANDED ABOARD. It does NOT mean missing, injured or deceased.** There is no new death, disappearance, family-census decrement or ship destruction authorization. The existing refusal of fatal child-bearing ferry incidents remains in place. No visual depiction of child harm or death is allowed.

The case header, all adult/child manifest member receipts and a count anchored in world_meta commit atomically under BEGIN IMMEDIATE with world-generation CAS. Injection after the header or after member writes rolls back completely. SQLite immutability triggers and independently reconstructed restore checks defend against accidental changes and inconsistent offline edits; they are not external cryptographic signatures.

## Historical recovery and checkpoint retention

Ordinary world snapshots retain genesis and three recent checkpoints. A committed case now **pins its exact observed snapshot generation** against future pruning. On reopen and independent recover_copy, the case is validated against that pinned simulation snapshot and its state checksum and canonical operation-log hash/tick, the actual applied stranding command and T31 transport envelope, all adults aboard at that historical generation, historical dependent boarding/arrival intervals, guardians, complete manifest counts, and the chain of rescue predecessor legs to the original transport.

Failure to find a pinned historical snapshot, complete manifest, immutable table/trigger, world_meta counter, stranding evidence or correct rescue-origin chain fails closed. Recording a case itself does not advance world time or change custody: a failed report leaves the **already committed** physical stranding intact. Snapshot pinning is intentionally limited to case generations, and long-term compaction/archive policy remains future work.

## Verified local tests

| Check | Result |
|---|---|
| Strict GCC C++17 Release, full regression suite | **27/27 passed** |
| GCC Debug + UndefinedBehaviorSanitizer, full regression suite | **27/27 passed** |
| Clean-source rebuild and all tests | **27/27 passed** |
| Final ZIP extraction and internal SHA-256 file manifest | **187/187 matched** |
| v45a → v45b1 source patch dry run | **Passed** |

New demog05q6e5b_incident_journal covers the original stranded ferry, failure after header/member writes and safe retry, duplicate and incomplete adult/child manifests, invented stranding event, historical pinned checkpoint survival across actual rescue/arrival, a second incident aboard the stranded rescue ferry, restart and independent genesis replay, removed child member, dropped ledger, altered counter, fake applied cause, forged original transport, missing pinned snapshot and refusal of replay with corrupt sidecar incident evidence. All 26 inherited regression tests passed.

## Deliverable

**Filename:** SingleGalaxy_DEMOG05q6e5b1_Journaled_Stranding_v45b1.zip
**Size:** 561,125 bytes
**SHA-256:** `4cd518df8966698b89c69d2dee73273d0a1fc88ac1948a402eda739ef4e0e731`
Includes entire focused kernel, v45a-to-v45b1 patch, 27 native test targets, Release/UBSan/clean build logs, policy and engineering report, 187-entry SHA256SUMS manifest.

## Unfinished: do not overstate this gate

- No actual missing-person finding, dependent fatality or permanent child-casualty ledger; absent evidence continues to mean corruption, not a death.
- Complete outcome partition (safe, rescued, unresolved with real evidence, fatal with separately verified family/census settlement) and authenticated guardian/receiving custody remains for later milestones. Start with nonfatal partitions.
- Only one ART02 family scope per stranding case; mixed-family ferry cases remain refused rather than silently undercounted.
- Detailed/warm-region workforce and industrial simulation handoff remains deferred; no main kernel merge, UI integration, native Windows acceptance, or full police/fire/EMS institutions. Full police/fire/EMS remains deferred as directed.
- No images, animations, cutscenes or visualizations depicting children being harmed or dying.

**Next:** DEMOG-05q6e5b2 — nonfatal survivor/rescue adjudication and authoritative receiving-guardian custody with durable rollback and region handoff protection, then separately verified casualty/family/population rules if needed.