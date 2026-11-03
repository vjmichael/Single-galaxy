# DEMOG-05q6e5b2 — Verified nonfatal rescue outcomes and receiving-parent custody

**Date:** November 2, 2026  
**Project:** Original Single-Galaxy C++17 / SQLite simulation; not EVEmu server.  
**Baseline:** Locally verified DEMOG-05q6e5b1 (v45b1).  
**Status:** Implemented and independently tested in the focused local **v45b2** source archive. **Not merged into the main playable game source, client or native Windows release.** GitHub remains a repository and engineering record, not the build environment.

## Outcome and custody contract

Each rescue attestation references a **previously committed nonfatal stranding case**, a successfully applied *HostEvacuatePassengers* world command, the original and receiving ferry/transport IDs, the complete set of children, and a real receiving guardian.

- The receiving guardian must be one of each child's original ART02 parent/guardian IDs **and must be physically aboard the receiving ferry**. No unrelated legal guardian reassignment is permitted or inferred.
- Historical source stranding snapshot and case roster must match exactly, with real passenger/child berths and the original ferry's rescue predecessor chain. Partial, duplicate or invented children are rejected.
- The evacuation command and transport envelope, historical applied event, operation-journal generation, canonical rescue snapshot, old-leg evacuation termination, new real receiving-ferry berth, immutable parent/child identities, and full adult receiving manifest must all agree.
- The host writes an immutable rescue outcome and one child receipt per listed dependent inside `BEGIN IMMEDIATE`, anchored by a persistent world metadata count. Failures after header or member writes roll back the outcome completely, allowing safe retry.
- Rescue generation snapshots are pinned against automatic pruning. Reopen and `recover_copy` independently validate cases, outcomes, receipts, command links, historical world states, guardians and manifest conservation. Forged/missing evidence is an integrity error—not proof of disappearance, injury or death.
- Read-only `verify_dependent_rescue_outcome()` returns independently checked **historical** guardian/evacuation evidence and **current** child custody, allowing rescued aboard, stranded on rescue vessel, and arrived to be distinguished without rewriting the past.

## Exact local test evidence

| Check | Result |
|---|---|
| Strict C++17 GCC Release, entire suite | **28/28 passed** |
| GCC Debug + UndefinedBehaviorSanitizer, entire suite | **28/28 passed** |
| Final ZIP independently extracted and rebuilt, entire suite | **28/28 passed** |
| Final source ZIP SHA-256SUMS integrity manifest | **201/201 matched** |
| Baseline v45b1 → v45b2 source patch dry-run | **Passed** |
| Adversarial rescue outcome rollback, replay, stale/incomplete/forged guardian/custody evidence | **Passed / refused as designed** |

The new `demog05q6e5b2_rescue_outcomes` test uses actual two-parent + dependent ferry travel, a journaled stranding event, a real evacuation to a second ferry, independent outcome attestation, both failure injections and safe retry, a second stranding aboard the rescue ferry, safe arrival, database reopen, independent genesis recover-copy, and deletion/alteration of case outcomes, parent receipts, rescue command, world metadata count and pinned rescue checkpoint. All 27 earlier regression targets remain green.

## Artifact

`SingleGalaxy_DEMOG05q6e5b2_Verified_Rescue_Outcomes_v45b2.zip` — **598,501 bytes**, complete standalone focused source, v45b1→v45b2 patch, 28 native tests, engineering report, Release/UBSan logs, independent extraction/build logs and per-file hashes.  
**SHA-256:** `ffa68078c8e3817dc1115d2fc58abfac7e9589cc7cfdd8d9a739f12d251eb482`.

## Unfinished and deliberately prohibited

- **Non-parent or changed legal guardian:** requires a separate independently authorized family/custody transfer. This gate verifies an original parent, not unrelated custodial adoption or guardianship.
- Mixed-family or split-passenger incidents remain refused by the focused manifest gate; no survivor/deceased partition has been added.
- **Dependent fatalities remain blocked**, and the prohibition on visually depicting children being harmed or dying remains mandatory. See `DEPENDENT_INCIDENT_NON_GRAPHIC_POLICY_2026-11-02.md`.
- Warm/detailed region ownership integration is still a conservative global active-dependent fence, not actual workforce/industry warm checkpoint reconciliation.
- Full game kernel merge, native Windows client testing, police/fire/institutional EMS remain unfinished/deferred per user direction.

**Next proposed gate:** DEMOG-05q6e5b3: authenticated legal receiving-guardian transfer with co-location and consent/policy receipt; then split rescue/region-scoped warm/cold transitions.