# COLONY-CONFLICT-01B3 — Physical NPC government, leadership succession and policy epochs

**Parent:** draft PR #24 (`CONFLICT-01B2`) dated diplomacy, PR #23 genuine subjective NPC grievances, PR #22 founding political agendas.
**Branch:** integration/colony-conflict01b3-governance-v85-20261103

## Contract
Every physical colony has a founding representative and agenda. Later owner-approved constitutional events append immutable governance epochs; they NEVER update founding rows or old strategic decisions. A handoff becomes active strictly when `epoch.world_generation < decision.world_generation`. Event ID numeric order does not define chronology.

Typed `GovernanceEpoch` records (`event_id`, `prior_event_id`, `ordinal`, `founding_event_id`, `colony`, `leader_id`, `agenda`, `DiplomaticPolicy`, `TransitionKind`) are returned by `colony_governance` as of the canonical physical generation.

Three admitted transition kinds:
- `Appointment`: a distinct, physically resident and duty-eligible adult successor replaces an on-duty incumbent with the authentic owner approving the appointment.
- `EmergencyVacancy`: the previous incumbent must actually be absent/unavailable and a different physically present successor must be duty-eligible. There is no fictional death/evacuation; only physical world state permits this gate.
- `PolicyRevision`: the same physically present on-duty leader changes political agenda and/or foreign-policy approach. No-op epochs are forbidden.

All changes require the real colony owner's approval. The call is trusted host-side, not a public NPC ballot or arbitrary client action. Democracy, voting, coercion, mutiny and election proof are NOT yet implemented; the event cause must not be mislabeled as an election.

## Policy effects
The `CONFLICT-01B2` strategic planner now resolves leadership **as of its historical world generation** and stores the active epoch ID in its original `agenda_event` field (founding event ID for the original government). Founder-only `01A` recording is rejected after a new government is effective, preventing bypass of new governance. Earlier accepted `01A` intent remains verified under the founding agenda and its original world snapshot.

`Pragmatic` preserves the prior diplomacy rule. `Reconciliatory` allows a Cooperative leader who hears a Hostile/Wary GRSM account to continue Observe rather than invent aggression: opinions do not turn into criminal guilt, and no old relation score is cleared. `SecurityFirst` issues an observable Security Warn for a non-allied neutral or friendly presence that would otherwise be merely Observed. Neither policy commands fire or confers a treaty. Actual Critical/Emergency resource conditions take precedence over all sentiment.

After transition, the newest actual leader is also the only valid recipient for NEW `CONFLICT-01B1` incident interpretations. The historical grievance journal and replay of old accepted claims still resolve the earlier leader from the old epoch.

## Persistence and independent historical verification
`sg_colony_governance_epochs` uses append-only SQLite rows with primary event IDs, a unique `(colony, ordinal)` epoch chain, and unique `(colony, world_generation)` publication fence. It binds exact source colony/owner, prior leader/policy/agenda and predecessor event, newly authorized leader/agenda/policy, transition reason, original physical world generation/tick/checksum.

Atomic `BEGIN IMMEDIATE` generation-CAS publication confirms all identities, owner approval and physical duty; it cannot allocate crew, create NPCs, alter inventory, fabricate diplomatic incidents or move residents. Duplicate same-generation publication is rejected.

Ordinary startup and `recover_copy` enforce both immutability triggers and reconstruct every constitutional event against its pinned ORIGINAL world snapshot, exact predecessor leader, physical eligibility, recorded owner and cause. `verify_colony_diplomatic_intents` recomputes prior policy and previously effective leader and rejects any mismatch in an old decision. World snapshot pruning preserves every governance publication origin in addition to already pinned diplomatic decisions and incidents. The tested attack model includes offline modifications with restored triggers, not a malicious operator replacing the entire complete authenticated history plus its external roots.

## Native acceptance
`tests/COLONY_CONFLICT01B3_GovernanceEpochTests.cpp` runs real C++ simulation/SQLite APIs, two colonies, three eligible residents in the transitioning colony, actual foreign ship and real wrecks.

Acceptance includes original founder’s expansionist warning and genuine −90 grievance; invalid cross-owner appointment, outsider successor, false emergency vacancy, same-person false appointment, no-op policy term, and duplicate same-generation term; actual appointment to a cooperative reconciliatory successor, which does NOT change an already archived founder warning; subsequent ship destruction interpreted by the NEW leader, changing the subjective target score −90→+10; same-leader policy revision to isolationist/security-first; actual appointment returning the founder as a new ordinal epoch; independent restart, SQLite recover_copy, forged-leader tamper rejection; preservation of all NPCs, ships and physical wrecks.

Focused CI `.github/workflows/colony-conflict01b3.yml` builds/runs previous DEF/01A/01B1/01B2 plus 01B3. **Verified focused native Linux CI:** [Actions run 41783146073](https://github.com/vjmichael/Single-galaxy/actions/runs/41783146073) tested executable revision `66d6c6d851719a0376cf69c65fb0b27f239a3f35` and passed **5/5 CTests**, with total native test time **2.95 s**: DEF-01A, CONFLICT-01A, CONFLICT-01B1, CONFLICT-01B2 and new CONFLICT-01B3. This documentation-only record does not change the tested executable. The additional family custody, safety, rescue and preceding conflict GitHub workflows reported success at the same executable SHA except for any jobs explicitly marked in progress at the time of reporting; the full integration matrix and Windows jobs remain independent gates.

## Not yet included
Actual elections, complex legitimacy or coups, independent NPC scheduling of leadership turnover, public treaty obligations, actual armed offensive fleets, manufactured defenses, ammunition, casualties, siege and risk-backed civil unrest are separate future gates. This version establishes honest durable constitutional and subjective policy state; it does not pretend to simulate a full RTS military or political economy.
