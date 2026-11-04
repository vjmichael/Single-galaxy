# COLONY-AUTO-04D.2 — Durable physical safety transitions

**Date:** 2026-11-03. **Branch:** `integration/colony-auto04d2-durable-safety-v69-20261103`. **Base:** v68 AUTO-04D.1.  
**Status:** Implemented on review branch; native/CI acceptance pending.

## Authority

`PersistentWorld::Store::commit` is the **single canonical SQLite writer**. Every committed world generation now calls `colony_safety::journal::record_world_commit` **inside the same `BEGIN IMMEDIATE` transaction** as the operation ledger, durable snapshot, physical custody, and other world effects. An exception before `COMMIT` rolls back both world and safety records.

The new journal is not a competing world store, and is not an authorization to execute repairs, mint supplies, or transport NPCs.

## Schema and semantics

- `sg_colony_response_meta`: one committed generation/tick/world-hash anchor, total transition count and current append-only hash-chain tip.
- `sg_colony_response_sites`: a per-compartment latest state; rebuilding it is allowed only in the canonical world transaction.
- `sg_colony_response_events`: immutable, sequential threshold changes, each tied to world generation, tick, home ship, compartment, population, old/new risk, physical **food/water/oxygen runway** witnesses, world-state hash and chained digest.
- Database triggers deny ordinary deletion or editing of historical response events. On startup the journal is checked for sequential IDs, correct hash chain, matching count/tip/generation/tick, and exact agreement of recorded compartment risks and populations with the current physical snapshot.
- Assess each provisioned colony ship, habitat, and farm compartment, without double-counting an overlapping habitat/farm site. The existing `ColonySafety::assess` remains the single risk calculator. A risk change generates exactly one journal transition for that world generation/site; an unchanged risk generates none.
- On a **preexisting save with no response journal**, the first new committed generation uses its previous physical snapshot as the baseline, without fabricating alerts for history before installation.
- Every successful generation, including command acceptance without a tick, updates the anchor. This is required to avoid a 'latest world generation vs journal generation' split on restart.
- The journal is verified before the transaction is allowed to commit and again when the game opens the save.
- A previously occupied compartment that vanishes is **rejected**, not journaled as a fictitious safe/evacuated site. A later explicitly verified death/transport/custody closure adapter must authorize that removal; this may block legacy destruction paths until integrated.

**Important:** FNV-based chained checksums detect accidental corruption and naive tampering but are **not an independent cryptographic signature** against a privileged attacker who can rewrite all database rows and recompute hashes.

## Acceptance coverage

`tests/COLONY_AUTO04D2_DurableResponseTests.cpp` checks:

1. Genesis has zero response events.
2. Committed food depletion causes distinct risk transitions at 16, 4 and 0 kg.
3. Physical crew still equals the original four people; planning creates neither stock nor rescue.
4. Injecting `FailPoint::AfterSnapshotWrite` rolls back world generation, tick and journal count.
5. Restart reconstructs the same 500-tick physical state and exactly three transitions.
6. Continued emergency does not create repeated alerts.
7. Mutation of an immutable event is refused by SQLite.
8. Privileged deletion after dropping the trigger is detected on next startup via count/hash-chain reconciliation.

`.github/workflows/colony-auto04d2.yml` requests native CMake/CTest checks for AUTO04A, AUTO04B native host + authenticated relay, and AUTO04D2. A committed test **does not** mean it passed; do not close the gate until runner evidence is available.

## Scope and remaining hard gates

- This delivers **durable threshold facts**, not a persistent outbound notification/outbox or proof of acknowledged NPC receipt. UI notifications may be derived from stored transitions but must have independently durable subscriber delivery/ack IDs.
- A cold/detailed region handoff still needs independent testing that generation/tick/site authority remains reconciled. Existing cold-region reducers do not yet publish these transition receipts.
- Only after a physical, capacity-/fuel-/route-/crew-/dependent-custody-verified action may a rescue or repair be marked executed.
- Do not infer player motive, automatic reputational punishment, or omniscient NPC knowledge from the objective journal.
- No player action can disable the hard life-support assessment or independent emergency intent.

## Next gate

**AUTO-04D.3:** durable outbox with per-NPC/per-colony observation opportunities and acknowledgment; then physically escrowed mutual aid, real authorized repair, and transport admission. A cold/warm handoff must preserve both the same resource inventory and exactly-once safety event identity.
