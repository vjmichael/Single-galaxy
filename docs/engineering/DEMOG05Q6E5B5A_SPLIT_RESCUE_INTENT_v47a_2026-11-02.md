# DEMOG-05q6e5b5a — Durable split-ferry rescue allocation intent (v47a)

**Date:** 2026-11-02
**Repository:** vjmichael/Single-galaxy (original game, NOT EVEmu).
**Baseline:** Local focused v46 C++17/SQLite kernel.
**Status:** **Implemented and tested locally in a separate focused source package; NOT merged into the complete playable game kernel, UI or native Windows release.** GitHub is documentation/source repository, not development host.

## Implemented

New host-only `PersistentWorld::record_split_rescue_intent(SplitRescueIntentPlan)` and `verify_split_rescue_intent` allow an immutable **allocation intent, not an evacuation**, assigning two different families aboard one actually stranded ferry to two distinct real receiving ferries and proposed new voyage IDs. Supports 2–8 family scopes, exactly 2 receivers, at most 16 passengers (named adults plus ART02 children) per receiver. Initially requires both birth parents to travel with each family, and disallows splitting a single family across receivers.

The writer checks full physical source passenger/child roster, authentic existing stranded voyage, each parent's original home and actual ferry custody, real ART02 births and complete dependent roster, single historical original voyage, stable Cold regional owner generation and family/region binding. Receivers must exist in the same system, be ready/co-located within docking range, empty of pre-existing passengers and dependent berths, and have distinct unused future voyage IDs.

One SQLite `BEGIN IMMEDIATE` transaction records proposal header, both receiver allocations, family and child receipts, all counts and a world-meta anchoring counter. The original exact world snapshot generation is **pinned against checkpoint pruning**. Fault-injected failures after receipt/member writes roll back completely. Restart and recover_copy recheck historical world snapshot, ferry state, parents, birth lineage, child physical leg intervals, all assigned receivers and manifest conservation. Missing receipts, absent triggers, forged region, removed child, altered new voyage or altered count fail closed.

The plan carries a length-prefixed deterministic FNV-1a fingerprint across its header and all child/family/receiver assignments. This detects inconsistent local offline edits but **is not a cryptographic seal**: an attacker rewriting all related anchors cannot be excluded by a checksum alone.

## Local verification (actual tool runs)

| Check | Result |
|---|---|
| Strict GCC C++17 Release full CTest | **33/33 passed** |
| Debug + UndefinedBehaviorSanitizer full CTest | **33/33 passed** |
| ZIP extracted into a fresh tree, independent Release build and full CTest | **33/33 passed** |
| Final ZIP per-file SHA-256 manifest | **239/239 matched** |
| v46 → v47a patch applied as `patch -p1 --dry-run` | **Passed** |
| Rollback, restart, pinned snapshot, independent recover_copy, offline tamper rejection | **Passed** |

The 33rd test uses two real families, four adult parent NPCs, two ART02 dependent children, a genuinely stranded source ferry and two real empty receiving ferries. It tests missing/duplicate family assignments, fake child, stale regional owner, duplicate receiving transport, two injected rollbacks, a committed allocation receipt, no actual custody movement, seven later world steps, restart, genesis recovery, and five offline corruptions with immutability triggers reinstalled. All 32 inherited regressions remain passing.

**Deliverable:** `SingleGalaxy_DEMOG05q6e5b5a_Split_Rescue_Admission_v47a.zip`, 713,384 bytes. SHA-256 `183ddd120fb34d9030e40eda3268e286953a3ca89a493e03a47fac710f1c2710`. Includes full focused CMake source, 33 tests, v46→v47a patch, engineering report, implementation acceptance contract, Release/UBSan and independent rebuild evidence, SHA256SUMS.

## Strictly not implemented

**Recording an intent does not move any family member. Split rescue execution is not enabled.** The v46 `HostEvacuatePassengers` world action closes the source voyage after the first actual rescue, so two sequential one-receiver evacuations cannot satisfy custody conservation. A new versioned single-action/two-receiver atomic simulation command, complete adult/dependent manifests, exact world-snapshot side effects, region fences, per-receiver voyage and child receipt journaling, fault/replay tests are required before enabling split movement.

Population/census consolidation in a shared production region, real cold-to-detailed workforce/industry checkpoint staging, integration into the full game and Windows tests remain open. Child-bearing fatality actions remain refused; **never depict children being harmed or dying**. Police/fire/full institutional EMS remain deferred at the user's direction.

**Next gate:** DEMOG-05q6e5b5b actual atomic two-receiver split evacuation and proof of every person's receiving custody.