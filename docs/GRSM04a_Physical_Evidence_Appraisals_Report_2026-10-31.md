# GRSM-04a — Physical evidence–linked NPC appraisals

**Date:** 2026-10-31  
**Scope:** bounded Linux-local prototype, no autonomous NPC decisions  
**Starting milestone:** GRSM-03d / SQLite v13; **result:** SQLite v14.

## Why this gate matters

Earlier social rapport and intergroup stances were host-attested interactions with an explicitly supplied Assistance/Harm interpretation. They were not yet derived from independently committed world effects. This gate connects the *opinion layer* directly to an actual physical effect through the preexisting incident ledger and a genuinely firsthand witness receipt. It does **not** assign fault or credit to an individual when the engine does not establish that actor.

## Implemented

- `PhysicalAppraisalStance` (`Beneficial`, `Damaging`, `Uncertain`) is a **person's interpretation**, not an objective moral label.
- `PersistentWorld::appraise_witnessed_incident(observation_id, context_group, stance, confidence)` requires a real local-observation knowledge record from the same current generation and tick, with a matching immutable physical incident. A relayed hearsay record cannot qualify, even when it cites a real incident.
- A firsthand witness can appraise a single incident **differently through two overlapping affiliations**. For example, witness 1 values a real ore movement as positive for the Mining Guild, but negative for a relief organization. Another local witness may remain uncertain. These appraisals never amend the cargo movement or source history.
- Group-specific appraisals require actual active membership and record the current append-only membership epoch. A subsequent leave does not erase earlier context; after restore, the original roster is reconstructed at that original epoch.
- Every `(observation_id, group_context)` may be appraised only once; the context `0` represents an unaffiliated personal reaction. Confidence is bounded 0–100 and does not assert objective certainty.
- SQLite v14 adds `physical_appraisal_meta` and immutable `physical_appraisals` rows, foreign-key relationships, immutable triggers and chained digests. Restore cross-verifies firsthand evidence, real incident tick/generation, membership epoch, continuity, chain length and tip. Recovery-copy preserves valid histories. Migrations from v13 start with **zero** historical appraisals and never fabricate previously unexpressed positions.
- No change was made to NPC casualties, cargo, credits, station inventories, physical events, sourced claims, group voting, or legal standing.

## Evidence and local acceptance

- Linux full source build: **passed**.
- Final full native/integration CTest: **64/64 passed**, 30.63 s, six-way parallel, 35s per-test watchdog (the earlier unbounded full-run was interrupted, with 22/64 cases visible; kept as investigation evidence).
- Targeted native persistence + all GRSM social suites: **11/11 passed**.
- Three repeated rounds of persistence, GRSM-03d, and GRSM-04a cases: **9/9 underlying invocations passed** (the CTest repeated report groups them by three selected cases).
- Node.js tests: **84/84 passed**.
- Python design tests: **3/3 passed**.
- Dedicated GRSM-04a scenarios: genuine cargo and firsthand witness, same incident assessed oppositely under overlapping memberships, no remote eyewitness, no hearsay-to-proof promotion, zero automatic group/rapport effects, old-v13 migration, retained history after leave, recover-copy and ten corrupted-DB cases rejected.
- Only Linux-local execution is established. Windows-native game installation, visual Chromium end-to-end play, massive population throughput and cross-host race stress remain open.

## Honest limits

1. **Physical incident is real, appraisal remains subjective.** The engine proves the ore moved or a casualty occurred; the host still supplies whether this witness finds that beneficial or damaging. That is not autonomous ethical reasoning or a model-based NPC personality.
2. **No invented perpetrator.** The physical incident's authoritative command actor is generally a ship or facility, not necessarily a specific NPC. No automatic attribution of personal culpability to a crew member or social organization is justified yet.
3. **No automatic social action.** Appraisals do not adjust directed interpersonal rapport, source credibility, a guild's official position, federation policy, a moral alignment bar, or a government legitimacy measure.
4. **Proximity and firsthand knowledge:** The existing witness gate is a local physical opportunity to observe, not an assertion of eyesight or complete scene coverage. Future line-of-sight and information routing can refine that.
5. **Integrity:** Receipt chains use non-cryptographic FNV-style digests and checks against other persisted state; a hostile writer with full database control could replace data and tip together. No authorization is conferred by this data model.

## Next gate: GRSM-04b — source-linked responsibility without overclaiming

Create verified effect/role receipts for physically committed actions that identify a responsible NPC or delegated crew **only when the game command/worker assignment actually proves that role**. Distinguish direct actor, beneficiary, victim, eyewitness and hearsay. Only then permit policy-controlled, bounded, individual attitude updates. Require tests against falsely naming a worker as culprit, retroactive blame, fabricated supplies, replayed rewards, and automatic remote awareness. Formal macro-group charters and institutional policy come later and must not overwrite member disagreement.

## Example of the path toward emergent gameplay

A mining ship transfers five units of ore (implemented canonical fact). An NPC who witnessed the transfer values it positively in the mining guild context (implemented host-supplied appraisal); the same individual values it negatively in the relief group context (implemented). Another witness expresses uncertainty (implemented). If a future real action ledger establishes that a named NPC stole the ore, **that separate event** could support evidence-based anger and group disagreement (not yet implemented). NPCs could then pursue competing policies or refuse orders once decision-making and organization governance exist (not yet implemented).
