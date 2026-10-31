# GRSM-03c.3 — Directed Rapport from Attested Social Interactions

**Single Galaxy · 2026-10-31 · Linux prototype**
**Acceptance status: verified bounded host-attested interaction slice, not autonomous NPC behavior**

## Why this gate exists

GRSM-03c.2 recorded genuine, co-located opportunities for NPCs to meet without generating friendship, hostility, testimony, or faction consent. GRSM-03c.3 adds **one explicitly attested social reaction** to a specific meeting while preserving a critical distinction: *liking a person is not the same as believing their reports*. Source credibility remains in the existing GRSM-03b directional trust history; subjective rapport now has its own durable chain.

## Implemented contract

- **SQLite schema v12:** non-destructive migration from v11; `interaction_meta` and `social_interactions` begin empty. Existing worlds receive no invented acts or relationship changes. Older fixtures are updated to remove v12 objects before constructing their genuinely older schemas.
- **C++ host API:** `record_social_interaction(encounter_id, observer_npc, actor_npc, kind)` accepts `Assistance` or `Harm`. It is deliberately **not** available as an ordinary player command or a background NPC autonomous decision. The host must first verify the actual helpful/harmful action in the future action-causality layer; **at present the label is host-attested, not independently proven by the physics/action engine.**
- **Physical eligibility:** the encounter is an extant GRSM-03c.2 record of the *same unordered NPC pair*, at the current simulation tick. Both still exist in the authoritative individual workforce and share a location. A remote person, wrong pair, stale encounter, self-interaction, or invalid kind is rejected without a side effect.
- **Directed scoring:** assistance changes the observer's rapport toward the actor by **+8** and harm by **−12**, clamped to [−100,100], neutral baseline 0. These are **provisional test constants**, not balanced gameplay design. One encounter may support one reaction in each direction, but never a repeated reaction from the same observer for the same encounter. Two NPCs can disagree about each other. The encounter itself has **zero** rapport effect.
- **Append-only durable provenance:** each social reaction stores sequence ID, encounter ID, observer, actor, action category, delta, final rapport, prior directed reaction ID, generation, tick and FNV-linked corruption-detection digest. The full reaction and metadata update occur in one SQLite transaction; database triggers deny ordinary UPDATE/DELETE.
- **Restore/recover-copy:** verifies count/tip, gapless IDs, encounter existence and participants, tick/sequence, per-directed-pair predecessor and aggregate rapport, effect bounds, and one reaction per direction per encounter. Where retained, physical snapshots are already checked by GRSM-03c.2's encounter validator. Deleted or altered rows cause restore to fail closed.
- **Read only:** `recent_social_interactions(limit)` returns 1..128 newest events; `directed_rapport(observer, actor)` reads the latest directed score, defaulting to neutral 0.
- **No cross-domain mutation:** reactions do not modify physical history, inventory, NPC casualties, claims, **GRSM-03b source trust**, or group membership. They cannot create jobs, allies, favors owed, legal punishments, or travel routes.

## Local validation

| Verification | Outcome | Evidence |
|---|---|---|
| Build | Clean incremental Debug Linux build | `docs/GRSM03c3_Full_Build.log` |
| New GRSM-03c.3 scenarios | 2/2 | `tests/GRSM03c3RelationshipTests.cpp` |
| Persistence + GRSM social regression | 9/9 suites | `docs/GRSM03c3_Social_Regression.log` |
| Complete native CTest | **62/62 passed** | `docs/GRSM03c3_Full_CTest_62_of_62.log` |
| Node.js | **84/84 passed** | `docs/GRSM03c3_Node_84_of_84.log` |
| Python design | **3/3 passed** | `docs/GRSM03c3_Design_3_of_3.log` |
| Intermittent-legacy relay group | **12/12 passed** in 3 rounds of 4 parallel tests | `docs/GRSM03c3_Relay_Stress_12_of_12.log` |

The relationship tests exercise two separated ships, explicit help and harm, asymmetric attitudes, wrong/remote/stale identity rejection, duplicate suppression, repeated encounters and reversals, save/load, recover-copy, v11 migration with retained meeting evidence, and deliberate metadata/row corruption. Legacy physical/cargo/knowledge isolation is checked.

## Guarantees and limitations

1. This is a **real C++/SQLite working prototype**, but its host-supplied `Assistance` / `Harm` label is **not independently derived from a physical work/combat/transfer event**. Proof-carrying action outcomes are the next important anti-fabrication gate. No claim of autonomous, self-directed personalities is warranted yet.
2. An encounter is an opportunity for social contact, not a transcript or evidence of a meaningful event. The host must invoke a social interaction separately. The game does not automatically award friendship for proximity.
3. Directed rapport is **not** a moral alignment bar, source reliability, legal verdict, faction opinion, reputation notoriety, or relationship obligation. Both positive and negative histories remain individually attributable.
4. Trust does not automatically change with rapport; a disliked NPC can still be credible. Test constants +8/−12 are not final balance values.
5. Digests detect ordinary corruption and missing rows, not a malicious writer capable of recomputing hashes. Startup validation scans reaction history rather than scalable APS cohort summaries.
6. The 62/62 Linux success and repeated relay passes do **not** assert Windows-native execution, browser UX completion, multiplayer or trillion-NPC performance.

## Next gate — GRSM-03c.4 / GRSM-04 prerequisites

Attach a social interaction receipt to a **validated simulation action** (e.g. verifiable aid, crew rescue, resource assistance, theft/harm), including actor and beneficiary, committed transaction, witnessed circumstances and a bounded interpretation step. Stop host attestation from being mistaken for canonical fact. Then test individual personality/relationship policy, grievances, mixed group interpretations and sparse APS summary promotion. Keep autonomously generated conversations, inter-colony communications and leadership politics separate until each has transport/evidence and scale gates.

**Development policy:** source/test locally; GitHub for source and milestone record only. No EVEmu runtime code, Lu or Cognitum changes.
