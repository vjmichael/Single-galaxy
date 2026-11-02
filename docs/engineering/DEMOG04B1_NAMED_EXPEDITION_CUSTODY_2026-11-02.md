# DEMOG-04b1 — Named pioneer expedition custody

**Date:** 2026-11-02. **Status:** locally implemented and tested C++17 **isolated** expedition-ledger enhancement. NOT integrated into authoritative gameplay PersistentWorld. NOT an NPC auto-colonization implementation.

## Native change

Evolves the DEMOG-04a SQLite expedition escrow with a durable, unique NPC ID custody registry. The registry is for *already counted people* and cannot mint additional colonists. Named manifests must enumerate every boarded individual. Once a source colony has named identity data, no anonymous manifest may bypass it; the local registry must cover all settled residents before a named voyage may reserve. Transition and identity movement share a single SQLite transaction and command receipt.

Named passenger states: at source, reserved, traveling, at destination, or permanently lost. Reservation rejects nonexistent, duplicated, already traveling, or non-source IDs. Replays bind to the exact sorted ID roster. Failed transactions roll back all names alongside group population, stocks, ship/site custody. Cancellation restores passengers; arrival retains their identities; ship loss marks them lost.

## Tested

- 500 starting residents with 500 seeded unique IDs; expedition of 100 people (70 adult / 30 dependent cohort).
- Complete launch → transit → arrival, restart before arrival, save/reopen, replay, duplicate IDs, conflicting IDs, forbidden unnamed mission, failed reserve/arrival fault injection, cancel, and permanent ship loss.
- Four selected native CTests passed: kernel, persistence, DEMOG-03 planner, DEMOG-04 pilot + named custody.
- ASan + UBSan native direct build with leak checking passed.
- Minimal 3-file source patch dry-run passed against the exact previous DEMOG-04a source.
- Conversation deliverable: `SingleGalaxy_DEMOG04b1_Named_Pioneer_Custody_Source_Tests_2026-11-02.zip`, SHA-256 `f154a1cd7f307b8fcae64128533f68682e4988a5c487ad89d412e40c6be90564`.

## Explicit limitations

- The prototype's independently seeded named registry is **NOT** validated against the actual `PersistentWorld::world().npc_records()` registry; ART-02b1 family, appearance, profession and history metadata are not yet moved.
- The existing game world and this prototype **must not independently debit or credit the same population/inventory**. A shared transaction/effect seal and authoritative cutover are required before gameplay enables it.
- Full game suite, cross-platform tests, and named population scaled to millions were not run for this isolated increment.
- Temporary bootstrap_named_npc is privileged fixture/integration setup only, not a permitted untrusted client action.

## Next milestone DEMOG-04b2

Build explicit identity and source-of-truth reconciliation with the real `PersistentWorld` snapshot, verify ship and stock entities, integrate ART-02b1 family record ownership, and implement one durable cross-region transfer/effect receipt with no dual population ledgers. Only after these tests should autonomously proposed voyages be actionable.

See [DEMOG-04a](DEMOG04A_DURABLE_PIONEER_EXPEDITION_ESCROW_2026-11-02.md) and [NPC pioneer design](../design/NPC_PIONEER_DIASPORA_COLONY_EXPANSION_2026-11-01.md).
