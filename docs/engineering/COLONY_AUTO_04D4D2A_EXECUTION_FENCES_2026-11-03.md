# COLONY-AUTO-04D.4D2A — Locked family and regional execution fences

Date: 2026-11-03. Follows v73 / PR #7 `COLONY-AUTO-04D.4D1`.

## Actual behavior shipped in v74

This is a **verification prerequisite** for an atomic family executor, not the executor itself.

`PersistentWorld::verify_prepared_colony_family_execution(plan, fence)` acquires `BEGIN IMMEDIATE` on the **canonical world database** and rechecks, together:

1. Current world generation, family proof, physical adult manifest and real child birth/guardian custody.
2. An existing immutable `sg_colony_family_departure_prepare` intent with byte-equivalent adult and child:guardian manifest, source, receiver, family scope, seed and canonical world-state FNV identifier.
3. Both ART02 family census revisions and complete source / receiver populations.
4. A same-database ECON03 checkpoint, stable **Cold** mode owner and generation, exact checkpoint revision, no pending events, regional populations matching ART02 and tick strictly earlier than the proposed execution step.
5. Refusal of stale owner token, stale world/family/region revision, missing or substituted intent, split family and warming/cooling transition.

Verification always **ROLLBACKs the read-only admission transaction**, including on success. It creates no transfer, no rescue receipt, no child travel leg, no artificial worker, and no resource. The native executor is *not* authorized by a standalone preflight: it must repeat these same checks inside its eventual effect commit.

`CheckpointStore::verify_migration_fence_in_world_transaction` shares the same cold-owner, revision, population, pending-event and tick semantics as the existing `commit_migration_in_world_transaction` implementation, without updating the checkpoint. It enforces that the active SQLite handle and checkpoint store refer to the same physical database.

## Tests and limitations

The v73 family proof now creates an independent ECON03 regional checkpoint and actual cold owner, verifies stable admission, then tests wrong revision, wrong owner generation, stale ART02 revision, substituted destination, missing immutable intent and freeze/release during a cold-to-detailed transition. It asserts that read-only verification never changed the authoritative region or moved a child.

Still **OPEN, NOT COMPLETED**: `04D.4D2B` execute one physical adult-and-dependent departure as a world command and journal child occupancy; `04D.4D2C` commit ART02 moves + regional population in the same world generation; `04D.4D2D` continuously debit actual food/water/oxygen for child occupants and independently recover after a crash, followed by invalid/legacy/warm-region acceptance.

No caller may treat `prepare`, `audit`, or `verify_prepared_colony_family_execution` as successful evacuation.
