# GRSM-03d — Individual Interpretations of Other Groups

**Date:** 2026-10-31  
**Repository:** `vjmichael/Single-galaxy`  
**Scope:** Linux-local prototype; a first bounded slice of directed, conflicting intergroup perceptions  
**Baseline:** GRSM-03c.3 SQLite v12, full test 62/62; new schema **v13**

## Goal

If Group A helps or harms Group B, individual members of B can interpret the incident differently, while Group A members may hold their own asymmetrical view of B. NPCs can belong to overlapping groups and have different group-context opinions of a target group. An institutional policy or macro-federation position **cannot** be inferred from those individual feelings.

## Changes implemented

- `GroupAttributionEvent`: immutable, directed member-level attribution of an existing GRSM-03c.3 social interaction, tied to source interaction ID, `observer_group`, `actor_group`, individual observer and actor, membership epoch, current generation/tick, prior attribution ID, stance, fixed bounded delta and current feeling.
- `GroupAttributionStance`: explicit host-supplied `Appreciative` (+8), `Resentful` (-12), or `Ambivalent` (0) interpretations. An attitude can disagree with the source's assistance/harm type by design because the observer's social valuation is subjective. **These labels have not yet been independently adjudicated against committed physical effects.**
- `PersistentWorld::attribute_interaction_to_group`: host/test only, same-tick existing source interaction; both NPCs must be real, surviving, co-located members of the named groups. Eligibility and membership are verified under an `BEGIN IMMEDIATE` transaction; one attribution per source interaction and directed group pair.
- `PersistentWorld::individual_group_feeling`: scoped to `(observer NPC, own group context, target group)`, never a universal reputation or legal standing.
- `PersistentWorld::group_relation_summary`: counts active members and the *distinct informed* members' latest views, including favorable/unfavorable/neutral and a mean over informed members only. Not an official group vote or ratified policy; uninformed people are not counted as neutral voters.
- SQLite v13 `attribution_meta` + `group_attributions`, append-only triggers, provenance digest chain, sequential IDs, membership-epoch provenance, source actor/tick/generation and prior-view reconstruction validated on restore and recovery. A v12-to-v13 migration makes **zero** historical group opinions rather than retroactively inventing them.
- `GRSM03dGroupRelationsTests.cpp`: contextual disagreement, reversibility through repeat explicit interactions, directional independence, overlapping memberships and the same event interpreted differently in two membership contexts; leave/rejoin summary behavior; negative API cases; recovery and SQL tampering; genuine v12 migration.
- Legacy schema-fixture tests updated to remove v13 tables when constructing genuine older schemas; current persistence-version assertion updated to v13. No preexisting gameplay resource/casualty rules altered.

## Test evidence (local Linux, Debug)

- Complete native and integration build: **passed**.
- Final complete CTest run after migration fixture correction: **63/63 passed**.
- Node.js viewer/contract tests: **84/84 passed**.
- Python design tests: **3/3 passed**.
- Initial complete CTest run reported **62/63** because a legacy v1 migration test still retained the new `attribution_meta` table while claiming `PRAGMA user_version=1`. This was repaired to remove both new tables from the fixture; the isolated persistence suite then passed, and the entire 63-test suite was rerun successfully. Retain that trace in the evidence archive.
- These are Linux-local results; Windows-native, manual Chromium UI, large population scale, replay under hostile database modification and true physical-source action classification remain open.

## Acceptance result

**GRSM-03d individual intergroup perceptions: accepted for the bounded Linux prototype.** Source actions do not create aggregate group judgments, trust, knowledge, group charters, votes or actual intercolony communication. An individual cannot instantly know about a distant event, because attribution requires an actual encounter-linked source at the same world tick.

## Design — nested groups are a separate subsequent gate

The user clarified the need for simultaneous loyalties and nested macro-societies. `docs/engineering/GRSM03d_Group_Opinions_And_Nested_Loyalties_2026-10-31.md` records the architecture. Distinguish:

1. Canonical physical truth vs a personal report and subjective interpretation.
2. One person's directed, context-specific view vs a descriptive distribution within a guild.
3. A guild's actually ratified position vs those surveyed members' opinions.
4. A corporation, colony or federation's negotiated macro-policy vs its subgroups' independent positions.
5. Familial/crew/union/guild/nation obligations, legitimate authority, fear and identity as different axes, never a global fixed good/bad score.

**Not yet implemented:** group leadership, faction elections, treaties/organizational nested edges, independently verified physical assistance/harm, autonomously selected stances, macro-group legitimacy, relationship aging or automatic communication routing. Future gate must first attach physical action receipts before inferred automatic social consequences are allowed.

## Security and recovery caution

Social digest chains and immutable triggers aid accidental corruption detection and detect many unauthorized row edits. They do not authenticate against a hostile SQL writer able to rewrite an entire chain and tips, nor grant models authority to mutate social records. NPC attitudes are personal beliefs, not authoritative truth.
