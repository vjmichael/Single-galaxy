# COLONY-CONFLICT-01B1 — Durable directed grievances and subjective diplomacy

**Branch:** `integration/colony-conflict01b1-grievance-belief-v83-20261103`
**Parent:** v82 `CONFLICT-01A` political agendas, v81 actual local NPC contact, existing GRSM physical incidents and individual knowledge chains.

## Why

A colony that can decide to confront a player still needs the ability to change its mind when events unfold. A burned ship is a single objective game fact; **the meaning of that destruction** depends on each colony's leadership, witnesses, information transmission, politics, fears and loyalties. This gate makes two separate colonies disagree about the *same actual incident* without overwriting that incident or declaring anyone criminal. Relations are **directed**. Praising one player does not force another settlement, the player or the attacker to reciprocate.

## Native behavior

`colony_conflict::relationship_delta(fidelity,valence,confidence)` weights a signed delivered NPC interpretation, never a fabricated firsthand account. Faithful retellings have 100% influence, embellished 60%, and contrary 40%. No first-person facts are assigned the speaker's desired emotional valence retroactively. Integer deltas are bounded to ±100, accumulated scores clamped to ±100. The score maps to `Hostile` (≤−50), `Wary` (≤−15), `Neutral`, `Friendly` (≥15) or `Allied` (≥50). These are current colony **opinions** and not recognized treaties, criminal convictions, permissions to fire weapons or guaranteed peace.

`PersistentWorld::assess_colony_relationship_revision(source,knowledge_id)` checks:
- a real founding political agenda with one named on-duty leader physically resident at the colony;
- a **transmitted** `npc_knowledge` claim actually received by that leader (parent ID, source/speaker/recipient chain, bounded valence/confidence and firsthand attestation are enforced by the pre-existing GRSM journal);
- the same immutable `physical_incidents` source incident with `PhysicalKind::ShipDestroyed`, not a rumor about an unverified attack;
- the original attacker entity/owner and preserved wreck at the incident-generation snapshot, not a claimant-provided name or target owner;
- genuine non-self foreign ownership and a single interpretation of that incident per `(colony,target_owner)`.

The query returns a candidate revision and changes nothing. `record_colony_relationship_revision(event_id,source,knowledge_id)` repeats the preflight under SQLite `BEGIN IMMEDIATE`, with current world generation CAS, and appends the signed interpretation to `sg_colony_relationship_revisions`, storing event ID, original world hash/tick, leader/agenda, immutable knowledge ID, incident ID and original incident generation, factual attacker owner, prior revision ID, previous score, signed delta, next score, source fidelity/valence/confidence. Unique constraints prevent replay of one knowledge or repeated strategic scoring of the same incident. An immutable update/delete trigger forbids editing the past. `colony_relationship(source,owner)` returns the current directed attitude based on the last independently audited revision.

`Store::verify_colony_relationships` runs on ordinary startup and `recover_copy`. It independently replays the complete prior-event/score chain; rereads each pinned original diplomatic-decision world snapshot and the original attacker-and-wreck snapshot; reattests the GRSM leader's actual received claim and world facts; and computes the same signed delta and new score from first principles. This prevents forged target attribution, score rewriting, altered emotional valence, missing revision links, duplicate counting, and deleting immutable guards. The relevant original world and incident generations are pinned in ordinary checkpoint pruning.

## Test

`COLONY_CONFLICT01B1_GrievanceBeliefTests.cpp` creates two independent real NPC colonies and a third-party attacker/victim pair. A host-only physical test command genuinely destroys a normal ship into a persistent wreck (the test does *not* expose player shooting or colony deletion). A named observer at each colony actually witnesses that one nearby wreck and transmits a distinct narrative to its local leader through the existing GRSM knowledge pipeline. One leader forms a +90 Allied view of the attacker; another records a −90 Hostile view of the identical actor and physically verified incident. No attacker automatically becomes a criminal and no colony fires weapons. A second actual ship destruction causes a new negative claim and revises the first colony from +90 to −10 Neutral, retaining the first account as immutable history. Wrong-colony knowledge, duplicate event counting and forged target ownership are refused. Restart after many world ticks, independent `recover_copy`, and privileged offline rewrite with restored immutability trigger must all produce correct acceptance or refusal.

## Intentional non-goals

**Not an automatic response scheduler yet:** trusted host must call the relationship revision API with an actual delivered GRSM knowledge record; no instant galaxy-wide dissemination. This first vertical slice derives owner attribution from existing actual *ship-destruction* incidents, not yet land combat, raids, diplomacy treaties, legitimate peace deals, theft/reciprocal repair, a player's gifts or every personal anecdote. The resulting attitude is a durable viewpoint **not wired directly into 01A's original immutable intent calculation**; that integration needs a versioned diplomacy epoch boundary so old strategic intents remain re-verifiable after later attitude changes.

**Leadership remains the v82 founding representative.** Succession, replacement, revolt, elected leadership, rules of engagement and changing colony charters require an append-only governance handoff where the new leader is physically present and the prior epoch remains independently replayable. Moving a leader without this gate must not silently transfer political power.

**Physical offensives remain future:** no weapons, raids, casualties, cargo seizure or resource-free instant wars can be triggered by merely reaching −100 Hostile. The player can negotiate, trade, retreat or retaliate after real later military systems exist.

## Verification

**Focused acceptance PASSED:** [GitHub Actions run 41780823938](https://github.com/vjmichael/Single-galaxy/actions/runs/41780823938), executable code/test commit `66440d5e3636f0568c512f5007cd6a3e2dfa7526`. All **3/3** real native SQLite CTests passed in 2.51 seconds: `galaxy_colony_defense01a_local_watch`, `galaxy_colony_conflict01a_aggressive_intent`, and `galaxy_colony_conflict01b1_grievance`. The hardening revision orders history by actual physical generation and per-colony relationship revision rather than externally allocated event IDs; the test deliberately uses **decreasing** event IDs for later revisions. This document edit does not change executable code. Broader PR workflows and integration review are separate acceptance gates.
