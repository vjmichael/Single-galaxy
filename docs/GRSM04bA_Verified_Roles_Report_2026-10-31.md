# GRSM-04b.A — Evidence-backed named NPC roles (first implementation gate)

**Date:** 2026-10-31  
**Game:** Original Single Galaxy prototype; Linux-local verification  
**Parent:** GRSM-04a, SQLite v14  
**Result:** SQLite **v15**; role receipt foundation, **not a completed proof of individual perpetration**

## Problem and design choice

Existing GRSM-04a facts are immutable cargo movements, ship travel, ship loss and named NPC casualties. A `Command.actor` is normally a **ship or facility EntityId**, and even the host-authorized `HostCrewLoss` command names a ship. This does **not** prove which individual NPC gave the order, piloted the ship, stole cargo, or harmed somebody. Therefore the initial GRSM-04b slice refuses to invent a perpetrator. It explicitly distinguishes **what evidence establishes** from **what a viewer might infer**.

## Implementation

- Introduced `VerifiedNpcRole::{Victim, FirsthandObserver}` and `IncidentRoleReceipt` public read API. No unproven `Perpetrator`, `Beneficiary` or `Culprit` enumerator is available in this gate.
- Host-only `attest_incident_role(incident_id, npc_id, role, observation_id)` uses one SQLite `BEGIN IMMEDIATE` transaction. The physical incident must have been committed **this same generation/tick**; a prior event is not newly certified after the fact.
- A `Victim` receipt requires a real `PhysicalKind::NpcLost` row with destination equal to the **same named NPC ID**, not merely a ship casualty tally. A victim need not survive to testify. Non-casualty cargo, ship destruction, wrong NPC, and missing incidents cannot produce this receipt.
- A `FirsthandObserver` receipt requires an existing local living NPC and that NPC's **own** direct-observation record, with no parent claim and matching incident, generation and tick. Hearsay, a remote NPC, a substituted observer, and a stale observation are rejected.
- New SQLite v15 `incident_role_meta` and append-only `incident_role_receipts` are integrity-checked on every restore. Digest chain and expected count detect ordinary alteration/deletion; independently committed physical and NPC-knowledge sources verify the roles. Replay of the same `(incident_id,npc_id,role)` is denied.
- Migration v14→v15 creates **zero old role receipts**. Existing physical appraisals, command journal, social views, trust and group memberships remain unchanged. `recover_copy()` is exercised with valid role history and rejects tampered receipts.
- A query `is_verified_incident_role` tests only whether an attestation exists; **it does not compute suspicion or assign guilt**.
- Existing v1→v14 migration fixtures were corrected to remove the new v15 role tables before asserting their original `user_version`; no migration fabricates retrospective proof.

## Linux acceptance

| Gate | Verified result |
| --- | --- |
| New GRSM-04b.A unit/integration cases | **2/2 scenarios passed** |
| Persistence and historical/social suites | **12/12 passed** |
| Native build | **All targets built successfully** |
| Complete final native and integration CTest | **65/65 passed**, one final invocation, six-way parallel, 30-second test timeout |
| Earlier segmented native invocations | **22/22 + 22/22 + 21/21** passed |
| Node.js client/viewer tests | **84/84 passed** |
| Python design tests | **3/3 passed** |

One earlier four-way parallel complete CTest attempt was interrupted by the command execution window after 54 tests had visibly passed; its log remains in the archive, **not counted** as success. The later complete final run passed all 65/65.

## Negative/tamper and recovery coverage

The new suite exercises a real mined cargo movement (no automatic individual culprit), a real named NPC death, valid first-hand witnessing, rejection of remote NPCs, wrong witnesses, relayed hearsay, no observation receipt, non-victim assertions, duplicate claims and stale events, surviving history after restart/recover-copy, v14→v15 with retained old physical appraisals and no invented role, then corrupted role, identity, incident, observation, timestamp, metadata count/tip, and deletion cases.

**Integrity limitation:** the digest uses deterministic non-cryptographic FNV chaining, not a signature. Full hostile control over the database can forge rows and recompute a chain; these checks are designed for source consistency and ordinary corruption, not cryptographic nonrepudiation.

## Remaining GRSM-04b gates

1. **04b.B — Actual NPC action execution receipts:** modify the game simulation/work scheduler to persist *who carried out* a real NPC job, with command/job identity, ownership, surviving worker ID, measurable effects and replay protection, in the same transaction as the effect. Source should be independently reconstructible; retrospective manual assignments cannot qualify.
2. **04b.C — Role separation and causality:** distinguish initiator, executor, direct victim, beneficiary, witness and secondhand reporter. Cross-check issuer (player) separately from NPC actors. An NPC merely aboard the ship or a guild member is not guilty by association.
3. **04b.D — Social interpretation from proven roles:** derive bounded candidate appraisal and rapport changes only when evidence is attributable; keep character opinion, observer confidence, direct evidence, legal standing and official institutional policy separate. Model conflicts and mixed incentives without instant propagation or a global good/evil meter.
4. Scale benchmarks, Windows-native install/UI gates and full autonomous decision-making remain later milestones; this Linux work does not imply their completion.

## Reproduce locally

```sh
cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
cmake --build build -j6
ctest --test-dir build -j6 --timeout 30 --output-on-failure
node --test tests/*.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

Local-first testing; GitHub is the source and documentation record. No EVEmu, Lu, or Cognitum runtime modules were imported. Only GRSM-04b.A is accepted here, not the overall GRSM-04b milestone.
