# COLONY-CONFLICT-01B2 — Generation-fenced diplomatic strategic decisions

**Branch:** integration/colony-conflict01b2-diplomacy-epoch-v84-20261103
**Parent:** PR #23 (01B1) immutable directed NPC grievances; PR #22 (01A) founding agendas; PR #21 local watch.

## Objective
NPC leaders react to what their settlement believes about a foreign ship owner, without rewriting older decisions when that belief changes. A grudge is not proof of a crime or a combat permission.

## Native behavior
The original `record_colony_conflict_intent` retains its old founding-agenda-only behavior and historic verifier.
The new host-facing `assess_colony_diplomatic_intent` / `record_colony_diplomatic_intent` adds only a previously committed relationship toward the *physically observed owner*, with strict `relationship.generation < decision.generation` fencing. Opinions recorded in the same generation cannot rewrite already accepted decisions. Externally decreasing event IDs do not change time.
Deterministic nonviolent rules: Hostile/Wary opinion raises a neutral Observe to Security Warn; Allied sentiment suppresses warnings/raid preparation to Observe but does not establish a treaty; Friendly opinion reduces predatory PrepareRaid to Warn. Actual Critical/Emergency shortages retain original physical survival priorities. All other cases preserve founding agenda-based motivations.
Each new intent writes an append-only `sg_colony_diplomatic_intent` row: original world generation, tick, state hash, observed contact, leader, agenda, target owner/ship, prior-epoch relation event and score, motive, posture and resource risk. Old and new decision ledgers reject double-consuming the same contact.
The startup and recover_copy verifier requires immutability triggers; recreates every archived world from pinned snapshots; checks original physical foreign contact and founding leader; and independently recalculates the as-of-epoch relation using already-audited GRSM revisions. Changing older opinions cannot alter earlier postures. Snapshot pruning pins original decision generations.

## Acceptance fixture
Two staffed colonies witness the SAME genuine ship destruction via on-duty guards and transmitted GRSM knowledge. One colony's actual leader records Hostile −90; another records Allied +90 for the same ship owner. Before the generation fence, both still use neutral strategy. After a world step, the former Warns and the latter Observes. A second real incident changes the first colony to +10 Neutral, while the historical hostile warning and previous legacy-neutral decision remain independently verifiable. The fixture tests duplicate contact consumption, no NPC casualties, restart, SQLite recovery and tampering of the frozen relationship score after restoring the immutability trigger.

## Non-goals and next gates
No firing, fabricated ammunition, cargo theft, raids, seizures or casualties. `CONFLICT-01B3` will require append-only leader succession/governance epochs with independently proven physically resident representatives. `DEF-01B/C` must supply actual staffed/powered/manufactured defenses, ammunition and damage provenance before combat operations are possible. Peace, surrender, reparations and treaties remain separate. Subjective opinions derive here from validated ship-destruction events, not arbitrary omniscient accusations.

## Verification
GitHub Actions `.github/workflows/colony-conflict01b2.yml` builds and executes four focused native SQLite/physical provenance suites. **Focused Linux CI PASSED 4/4 native CTests** at executable commit `9b30fa25b9d6a9eb0afb7779cccf31170ec350fe`: [Actions run 41781982087](https://github.com/vjmichael/Single-galaxy/actions/runs/41781982087), 1.60 s test execution, all four test targets compiled. Native local watch, founding aggressive intent, subjective grievance, and new dated diplomatic intent acceptance all passed. This documentation-only revision does not change executable C++ code. Broader regression, Windows, physical defense and governance succession remain unverified.
