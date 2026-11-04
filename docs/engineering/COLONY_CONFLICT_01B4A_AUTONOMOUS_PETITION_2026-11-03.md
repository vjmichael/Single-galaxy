# COLONY-CONFLICT-01B4A — Autonomous, nonbinding NPC emergency reform petition

**Branch:** integration/colony-conflict01b4a-emergency-petition-v86-20261103
**Parent:** COLONY-CONFLICT-01B3 approved physical governance epochs (draft PR #25).

## Purpose and authority
The colony does not stay passive when survival conditions deteriorate under a non-cooperative or security-first administration. Its real local adults can collectively constitute an observable crisis caucus and raise a reform proposal **without any player or host selecting the nominee, motive or claimed participants**. However, **no simulated voting, independent NPC personality, election, coercion, transfer of authority or actual government replacement occurs in this gate**. The owner retains final constitutional authority through the existing B3 transition API.

## Deterministic assessment
`assess_autonomous_colony_petition(colony)` is read-only. It requires: (a) a real founded physical colony with surviving resident, duty-eligible incumbent; (b) actual `colony_safety::assess` Critical or Emergency shortage; (c) active historical government with non-Cooperative agenda or SecurityFirst policy; (d) at least **two distinct physically present, available adult NPCs other than the incumbent**. The full roster is drawn from authoritative `Simulation::npc_records()` and independently rechecked against duty and custody. It is a caucus **participation roster, not a list of votes or consent**. The lowest eligible NPC ID is used as a **deterministic nominee/spokesperson**, not a proven elected winner.

The candidate proposal changes agenda to Cooperative and policy to Pragmatic for crisis response. This is an advisory request with no automated power to alter resource flows, inventories, life support, player diplomacy, military targets, public treaties or population records. Cooperative pragmatic leadership is not arbitrarily challenged by this initial emergency-reform rule.

## Durable publication
`record_autonomous_colony_petition(event_id,colony)` is the trusted-host scheduler entry point: the host allocates only a fresh event ID. The canonical SQLite writer uses `BEGIN IMMEDIATE`, current-world generation CAS, identical assessment under the transaction, and one append-only `sg_colony_political_petitions` row with original world generation/hash/tick, source, old government epoch, incumbent, physical nominee, actual roster/count, active agenda/policy, physical scarcity risk and proposed reform. Uniqueness by `(colony,governance_event)` prevents repeated spam during a single governing term. Two update/delete-blocking triggers protect the original evidence.

Every normal restart and `recover_copy` loads the pinned original world snapshot and **recomputes** the pressure: missing source, forged eligibility, arbitrary fabricated supporters, wrong incumbent, false risk, changed proposed policy or altered spokesman are refused. Snapshot pruning pins the generation. Successive different leadership epochs can each potentially generate their own historically justified petition.

## Test fixture and expected outcome
`tests/COLONY_CONFLICT01B4A_EmergencyPetitionTests.cpp` creates three native NPC colonies: a predatory four-adult colony, a cooperative three-adult colony and a one-adult expansionist colony. They all have finite real food/water/oxygen stores. Early in safe operation no petition is valid. At tick 980 after material consumption, the predatory colony under actual Critical shortage produces a genuine three-person local caucus and a deterministic candidate; the cooperative colony and under-staffed remote colony do not. No automatic political change occurs on petition publication. Duplicate petitions and wrong-owner attempted appointments are rejected. The owning player may then independently approve the proposed leadership and cooperative policy through B3, which takes effect only after the next world generation. Acceptance checks no NPC casualties, normal restart, backup/recover_copy and privileged tampering with immutable nominee while restoring its update guard.

## Explicit limits / follow-ups
This is a **first autonomous institutional-pressure gate**, not a true vote or independent personal political preferences. There is no implicit proof that roster members endorse the proposal, so application code must never label this outcome an election, a majority, or a referendum. A future `01B4B` may add explicit persistent NPC intent/ballots with independent voter identity, abstention, proposer affiliation and true governance approval linkage; `01B4C` can add durable player response, referendum/treaty semantics and lawful constitutional authorization. No violent overthrow, military mobilization or crew identity creation.

## Verification
Focused six-suite workflow `.github/workflows/colony-conflict01b4a.yml`; real CI status must be recorded from the actual run. Broader Linux and Windows integration stay separate.

**Observed focused Linux acceptance: PASSED 6/6 native CTests** at executable commit `853d5199298a30fe0171b8df2d12d4f239aedb39`: [Actions run 41784134206](https://github.com/vjmichael/Single-galaxy/actions/runs/41784134206), 4.82 s CTest execution. Original physically witnessed colony contact, predatory intent, grievance interpretation, historical diplomacy, governance succession and new nonbinding political-pressure caucus tests all passed. Documentation revisions are not new executable verification.
