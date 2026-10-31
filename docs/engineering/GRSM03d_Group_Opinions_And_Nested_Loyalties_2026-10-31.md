# GRSM-03d — Nested Affiliations, Conflicting Loyalties and Group Perspectives

**Date:** 2026-10-31. **Development model:** original Single Galaxy, Linux-local-first. This RFC separates implemented prototype foundations from proposed future simulation.

## Principle: neither people nor institutions are hive minds

An NPC can belong to several overlapping groups: family, crew, labor guild, trade association, colony, defense corps, corporation, religious institution, regional coalition, or macro-federation. None automatically outranks all others. An NPC may feel affection for a community, distrust its leadership, depend on its services, reject one of its policies, and still risk their life defending its members. A group can contain simultaneously admiring, skeptical, angry, indifferent and misinformed subcommunities.

Three separate phenomena must never be conflated:

1. **An NPC's personal view of another NPC or group** (subjective and directional, evidence/provenance linked).
2. **A descriptive group distribution** (bounded aggregation of members who have formed a view; includes informed/uninformed counts and disagreement; *not* a vote).
3. **A formal group policy** (an institutionally ratified position attributed to a real decision procedure, with delegates, legitimacy, dissent and a durable record). A parent or federation policy is a *fourth* procedural product, not the averaged opinions of all its members.

## Current GRSM-03d prototype slice

- SQLite v13, bounded **individual** group-attribution receipts anchored to an actual same-tick GRSM-03c.3 social interaction. The observer and actor must be members of the named source/target groups at attribution; row records frozen membership epoch. No fabricated interaction or off-world communication is created.
- Three *host-supplied interpretive stances*: Appreciative (+8), Resentful (-12), Ambivalent (0). The stance is a personal interpretation of an already attested interaction, **not** a physically verified fact or moral/authoritative evaluation.
- A member can interpret one interaction through several legitimate, overlapping memberships with different conclusions. Memberships and intergroup evaluations are directed: A's members' views of B need not match B's views of A.
- Descriptive `group_relation_summary(from,toward)` reports active roster count, informed speakers, favorable/unfavorable/neutral counts and mean among informed; it does **not** create a faction mind, vote or sovereign judgment. Uninformed members are not counted as neutral believers.
- Source interaction, membership history and per-member group attribution are immutable and provenance-validated across save/recover. Pre-v13 worlds begin with no fabricated group positions.
- No automatic gossip, action-derived help/harm, trust updates, macro-group governance, factions, legislation, votes or commitment to a group is yet implemented. These are separate engineering gates.

## Future layer: contextual, competing loyalties

Record *relationships to affiliations* as a bounded profile rather than a single moral/allegiance bar: shared identity, emotional attachment, legitimacy judgment, reciprocal obligations, practical dependence, fear, shared values and relationship to leadership. They can move in different directions following verified experience; avoid presuming identical meaning between species/cultures.

Decision selection is **context dependent**: evaluate the active role, physical risk, current dependent NPCs, moral concerns, legitimate promises, feasible alternatives, cost of dissent, uncertainty and strength of independently documented obligations. For example, loyalty to the colony can motivate an engineer to oppose a colony administrator's order while still protecting colony civilians. Allow conscientious refusal, dissent within obedience, whistleblowing, mediation, neutrality, and faction exit—not just allegiance flips.

Initial priorities should be **specific to the choice**, not an automatic global weighted sort of organizations. Tie-breakers may include: direct responsibility for dependent NPCs; immediate, documented danger; explicit duty signed by the NPC; whether a lawful authority is recognized; current beliefs about the underlying evidence; hardship to friends or crew. Higher loyalty does not guarantee obedience.

## Future layer: groups within macro-groups

Groups may be nested or jointly affiliated through explicit, dated organizational edges: member organization, parent organization, cooperating treaty partner, competitor, political opposition, subcommittee. Different relations carry different authorization and obligations. A group can participate in multiple trade leagues but only one legally recognized colony administration, where such a rule exists by actual group charter.

Groups may **describe** other groups through their members' perception distributions, but only a real chartered process can **ratify** an institutional policy. A macro-federation can officially defend a guild while its security branch condemns its conduct and its civilian branch expresses relief. No parent institution learns individual testimony unless a proven communication, hearing or archival import occurs.

The group graph should support **time-bound memberships and cross-membership**: persons change groups, organizations secede, committees split and regroup, and parent/subordinate ties can be contested. Do not make a one-to-many tree a fundamental assumption. Use a directed, provenance-linked graph of organizational relationships.

## Example: three mutually consistent viewpoints after a rescue

A mining guild and a defensive patrol belong to the same federation. A patrol saves trapped miners but diverts supplies from a distant agricultural colony.

- Miner #4: grateful toward the patrol for their rescue, distrustful of its commander for earlier lies.
- Miner #5: bitter because a sibling was left behind, yet continues to defend the federation.
- Miner #6: lacks direct evidence and withholds judgment; rumors must be sourced and traveling should take time.
- Guild members are divided; a guild committee can nevertheless pass a formal gratitude resolution while noting dissent.
- Agricultural workers condemn the diversion, although they may still support rescue policy generally.
- Federation authorities negotiate an aid budget; an official policy is not equivalent to the average of its organizations' sentiment.
- A medic belongs to both patrol and guild; they can support rescue training, reject unsafe command orders, and publicly defend both institutions' workers.

This requires **distinct source truth**, **personal belief**, **descriptive social distribution**, **institutional policy**, and **macro policy**. Contradictions are features of individual interpretation, not corruptions of canonical reality.

## Performance and safety constraints

- Never iterate a huge population every world tick for sociological bookkeeping. Generate candidate reactions from bounded physical events and controlled communication deliveries; cache group summaries incrementally or calculate them for bounded rosters, maintaining sparse APS cohort evidence when population scaling requires it.
- The provenance ledger records who saw or learned which claim, when/where, with allowed privacy and confidentiality. An NPC cannot know a remote faction's internal position by cosmic implication.
- Official position must require a validated governance action or charter-defined representative procedure; no automatic dictator or instantaneous consensus, and no NPC's conscience is overwritten by the result.
- **Group aggregates are descriptive**; a popular false story does not become true through repetition. Distinguish group favor, respect, admiration, fear, economic trust, legal standing and historical claims.
- Hash-chain integrity checks are corruption signals, not a cryptographic security boundary against malicious SQL writers.

## Suggested follow-on acceptance gates

1. **03d prototype acceptance:** distinct member interpretations of other groups, overlap-specific views, directional asymmetry, leaving/rejoining group, migration, recovery, and tamper rejection. Full final Linux regression must be rerun.
2. **03e action provenance:** replace host-supplied assistance/harm and stance assertions with independently verified physical effect receipts before any automatic social consequence.
3. **03f formal organizations:** chartered voting/ratification, bounded dissent/leadership actions, contingent rights and responsibilities, explicit parent-group ties and federated policy routes.
4. **04 autonomous society:** local labor incentives, time-bounded friendships, mutual aid, defect/mediate/exit choices, contradictory faction narratives, demographic/infrastructure constraints, sparse APS scaling and later full gameplay/Windows checks.

This RFC is not an assertion that the game already supports these higher-order society behaviors.
