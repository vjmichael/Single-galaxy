# COLONY-EXIT-01 — Voluntary NPC departure replaces political petitions

**Decision: November 3, 2026.** This is a new branch from COLONY-CONFLICT-01B3. The unmerged NPC-petition PR #26 is explicitly superseded and closed; **no voting or petition functionality is included**.

## Gameplay direction

Disgruntled individual colonists express agency by seeking to leave a colony, not by petitioning or voting for government replacements. The player appoints administrative leadership and sets policies. Mistreatment, real survival shortages, injury, unresolved grievance or harmful decisions can have consequences for retention, individual trust, skills, maintenance, production, families and colony viability. Historical GRSM reputation remains subjective and anchored to real events rather than manufactured player guilt.

## Required eventual departure state machine

1. A real named resident experiences physical or social events. Distinguish personal witnessed injury/shortage from rumor; never attribute shortages to malicious player intent without evidence.
2. Individual dissatisfaction persists with recovery, thresholds and hysteresis so one bad tick cannot trigger a mass departure.
3. Individual seeks an actually viable alternative home. No colony-wide vote, petition, forced leader replacement, or fabricated consensus.
4. A destination genuinely accepts settlement; ferry, capacity, provisions, route and associated risks are verified. When no safe route exists, NPCs remain physically present and seek shelter/rescue rather than teleport.
5. Family/guardian/dependent closure is enforced. No separation of guardians and children, silent loss of patients, or duplication of crew.
6. Departure uses existing physical embark, voyage, disembark, and migration/ART02/ECON03 settlement receipts. Regional, individual identities and occupation changes are committed atomically. Services degrade when workers actually leave.
7. The individual's memory persists and their new colony can learn about the old relationship only through actual communications.

## EXIT-01A implemented scope — survival pressure, not emigration

The first narrow gate records an **objective, individual, physical life-support pressure witness**, rather than inventing an NPC's subjective consent or completed departure. The source must have genuine Critical or Emergency life-support risk. The resident must be a named living, physically present, duty-eligible adult with no active transport; the current leader is excluded to preserve continuity of authority. A safe colony, a foreign-resident ID or a nonexistent ID cannot pass.

The read-only assessor and host-side record methods are `assess_colony_exit_pressure(colony,npc)` and `record_colony_exit_pressure(event,colony,npc)`. The durable SQLite table sg_colony_npc_exit_pressure contains the exact source world generation/hash/tick, owner, government epoch, resident identity and verified risk. A UNIQUE constraint on colony/NPC/government epoch prevents repeated nagging, and update/delete triggers enforce immutable evidence.

Every startup and recover_copy re-evaluates the evidence against its pinned original snapshot and reconstructs the historical effective government. Snapshot pruning preserves these origins. Recording this evidence does not change population, create travelers, authorize departures, change leadership, accuse the player, or fabricate a ballot.

Native test `tests/COLONY_EXIT01A_SurvivalPressureTests.cpp` checks a threatened 4-person colony, a safe 3-person colony, a one-person colony, individual eligibility, duplicate refusal, unchanged headcount, restart, recovered copy, and rejection of offline tampering of the named NPC.

## Integration constraints

The existing HostColonyDeparture path evacuates an entire adult colony under strict local risk and custody restrictions. It is **not** a primitive for arbitrary individual departures. SocietyMigrationPlan works after real embark/disembark with ART02 family and ECON03 regional reconciliation; never bypass its receipts. Future integration must also cover children, legal guardianship and medical cases with atomic cross-store settlement, safe destination acceptance, real transport and workforce reduction.

EXIT-01B: individual dissatisfaction and recovery from evidenced actions. EXIT-01C: actual route, ship, destination, and household closure. EXIT-01D: durable executed physical departure and society/workforce reconciliation. No single tick of bad conditions or fictional transport should remove a resident.

Dedicated native CI: .github/workflows/colony-exit01a.yml. Do not claim full departure functionality or Windows acceptance from these tests.

## Verified native test results

**6/6 focused Linux CTests passed** at executable revision `496b2e151b760585cee78ffbca194b1d7b6d085b` ([Actions run 41785467590](https://github.com/vjmichael/Single-galaxy/actions/runs/41785467590)). The native suite exercised physical colony watch, strategic intent, GRSM grievance, dated diplomacy, dated government succession and the new individual NPC survival-pressure witness in 3.63 seconds. No physical departure or Windows hardware check is inferred from this result.
