# COLONY-AUTO-04D.5B — Durable NPC Emergency Supervisor

**Branch:** `integration/colony-auto04d5b-durable-npc-supervisor-v79-20261103`

**Date:** 2026-11-03 Arizona. **Version:** v79. **Parent:** v78 host-supervised observed-NPC dispatch; v77 verified multiple guardians; v76 atomic world/ART02/ECON03 family evacuation.

## Goal: actual host-loop autonomy, not another immutable story

v78's NPC coordination could read a genuine delivered shortage notice, find a safe nearby recipient, and admit a real command. However, admission did not retain the complete transfer plan. If the host died immediately afterward, the world still held a pending evacuation command but the host had lost its family, guardians, member event IDs and regional CAS tokens. A story that says "NPCs saved the family" would be dishonest.

v79 makes the **entire accepted rescue plan durable in the same world transaction** as that accepted command. This plan can be reconstructed after process restart with no caller-provided command ID or retained in-memory state. The supervisor periodically examines the real local, latest shortage notification and may initiate the next authorized rescue.

## Host-side loop and authority

`PersistentWorld::supervise_autonomous_colony_emergency(route)` is intended to run from a trusted host scheduler. The region service supplies a bounded `AutonomousSupervisorRoute`: source ship and authoritative family/region bindings; preallocated unique command, intent and per-member event IDs; and eligible region-bound receiving ships. No player API or unauthenticated model may invent these bindings or use this route to force an unsafe evacuation.

The loop's priority is:

1. **Resume** a previously admitted, not-yet-applied NPC rescue from SQLite. A process restart does not require the original C++ request or a remembered AI message.
2. If a family was already physically evacuated, report completion and do not dispatch a duplicate.
3. Evaluate the *current* physical source shortage. A safe colony does not trigger a new emergency.
4. Read the most recent committed real `sg_colony_response_events` for the source compartment and find a currently resident, living named NPC in that notification event's local opportunity roster. Deterministically use the first eligible NPC ID; never treat opportunity as a proven belief.
5. Trusted host delivers the notice via existing `acknowledge_colony_notification`, which is durable and idempotent. A crash **after delivery but before rescue dispatch** is safe: the next loop can find the same latest event even though the acknowledgement already exists.
6. Let v78 select the **nearest fully viable actual receiving ship**. Its strict safety and provenance requirements remain in force: full living family, legal guardian, two ART02/ECON03 reconciled populations, stable cold-region owner, geographically reachable same-owner ship, and at least four future real consumption periods of food/water/oxygen for everyone aboard.
7. Without a real safe receiver, report `AwaitingPhysicalRescue`. The physical shortage, delivered-NPC observation, and normal emergency response/distress policy remain durable; **do not mint resources, pretend a repair occurred, or claim arrival**. A later poll can retry with changed real supplies or ships.
8. Once a verified recipient exists, the supervisor durably queues that real host command and the whole transfer plan, then reports `RescueQueued`. Actual adult/child movement is still exclusively `step_with_atomic_family_departure`.

The host can call `resume_next_pending_autonomous_rescue()` directly if its startup path should finish outstanding work before accepting new commands. It finds at most one pending rescue at the current admitted world generation, reconstructs its whole plan, and invokes the existing v76 one-transaction world + ART02 family + ECON03 regional custody transfer. Already completed requests are idempotently no-ops; competing pending plans fail closed. The region-scoped `supervise_autonomous_colony_emergency(route)` checks the saved pending command's originating source and family/region scope **before** allowing recovery. If another colony owns the pending command, it reports `RescuePending` without executing that unrelated family's rescue. This is explicitly tested.

## Durable normalized proof

During the **same** SQLite `Store::commit` as the accepted world command, v79 publishes:

- `sg_colony_npc_supervisor_activation`: immutable version boundary. This permits old v78 dispatches with no plan to remain valid historical records; they cannot be misrepresented as resumable v79 plans.
- `sg_colony_npc_supervisor_plan`: one immutable entry per accepted command, including the original prepared intent, exact admission generation, family seed, source and destination colony identities, regional owner/revision and both ART02 census CAS revisions, and adult/child counts.
- `sg_colony_npc_supervisor_member`: bounded, ordered, canonical full manifest: all adult IDs, every ART02 high-ID child, original physically verified guardian, and every unique preallocated immutable movement event ID.

All three tables have update/delete immutability triggers. Failure injection at `AfterSnapshotWrite` rolls back the accepted command and all three proofs together. The separately committed prepared manifest is only a proposal and moves nobody.

Independent `Store::verify_supervisor_plans` validates activation, schema/trigger integrity, header/member completeness, globally unique event IDs and nonduplicated persons, canonical sorting, each guardian in the complete adult roster, exact match to original immutable `sg_colony_family_departure_prepare`, equality with the original accepted command in `operation_log`, and equality with *all* adult/child actual physical identities and guardians in the preserved admission snapshot. A damaged member ID, missing row, rewritten guardian or forged event cannot be accepted as a genuine restart plan.

v79 also **pins original NPC admission snapshots during automatic snapshot pruning**. This repairs a potential v78 long-running-world provenance gap, where historical admission snapshots could previously be pruned after several unrelated commits.

Independent verification runs on ordinary world restart, `recover_copy`, a positive `verified_atomic_family_departure` query, and each pending-plan lookup.

## Acceptance gate

`tests/COLONY_AUTO04D5B_DurableSupervisorTests.cpp` creates three real colony ships, actual source parents, one actual ART02-born and physically registered child, independent family/region census, and limited source provisions. The test drives an actual shortage, requires no autonomous response while safe, observes automatic NPC selection and durable delivery, and refuses a non-existent regional recipient. It then tests:

- no available safe ship -> durable warning remains and *nobody relocates*;
- crash/failpoint after accepted-command snapshot -> admission and normalized recovery plan both rollback; an earlier delivered notification remains available for retry;
- nearest safe receiver chosen on retry, complete canonical guardian roster durably admitted, but nobody moves during admission;
- **restart #1:** retrieve immutable complete plan with no original host C++ request, inject another failpoint halfway through world/ART02/ECON03 execution; physical source population unchanged;
- **restart #2:** call only supervisor route; it resumes the *existing* accepted command, verifying family relocation once and refusing duplicate dispatch;
- after many more world ticks, all three authorities still reconcile and the original admission receipt remains independently inspectable;
- normal restart and `recover_copy` retain the completed family and original NPC decision;
- privileged tampering changes the rescued child's saved guardian **and reinstates the immutability trigger**. The independent verifier must reject the semantic mismatch, not merely detect the missing trigger.

The v76 and v77 custody and v78 autonomous decision tests are part of v79's focused CI regression suite.

## Explicit current limits

- The host still must **invoke** the supervisor loop with trusted routing and preallocated IDs. This is a durable deterministic NPC coordinator, not a permanently running OS background process or an LLM generating orders without host authorization.
- The controller requests supplies, repairs and distress by returning the existing validated physical `ResponsePlan` and preserving the warning; it **does not yet execute** a resource-backed repair, hauling/supply transfer or radio distress work order. Those require separate real cargo, labor, logistics and notification commits and must never be represented as completed on request alone.
- Only a same-owner, same-system, *fully registered*, locally reachable household with verified stable cold owner and sufficient receiving needs is automatically rescued. Remote ferries, multiple-region transactions, specialist staffing and unproven nonparent custody stay refused.
- **Adult-only colonies without any ART02 dependent children are not routed through this v79 supervisor yet.** The already existing separate v58 adult-only departure gate handles their physical movement but does not automatically perform this supervisor's full family/ART02/ECON03 custody transaction. A verified adult-only supervisor pathway is a follow-up, not a successful case of this acceptance.
- A mixed cohort of NPCs in multiple family scopes must be resolved as a complete authority transfer, not stripped of missing dependents.

**Original focused acceptance PASSED:** [GitHub Actions run 41774303537](https://github.com/vjmichael/Single-galaxy/actions/runs/41774303537), compiled real SQLite-backed `galaxy_kernel` and passed 4/4 named CTests: `galaxy_colony_auto04d4d2b2_atomic_family`, `galaxy_colony_auto04d4d2c_mixed_guardians`, `galaxy_colony_auto04d5a_autonomous_emergency`, and `galaxy_colony_auto04d5b_durable_supervisor`. New v79 test passed 1.07 s, full selected matrix 3.65 s. Tested code commit `980d2f311a6eaab2cccdbf766c8d79202ce4800e`. This documentation edit does not change compiled source.

Keep the pull request draft pending the wider predecessor CI and independent integration review; do not treat physical supply transfer and repairs as implemented based on a rescue dispatcher alone.

**Additional multi-colony route-scope regression:** source/family/region-scoped polling now refuses to seize an already-admitted evacuation belonging to another colony. See `tests/COLONY_AUTO04D5B_DurableSupervisorTests.cpp` for the wrong-source pending-command assertion. **Verified final executable-code rerun:** [GitHub Actions run 41774656947](https://github.com/vjmichael/Single-galaxy/actions/runs/41774656947), commit `712f5fadd9901e4368abb61639a2cd4e1e33cf90`, compiled and passed the same four real SQLite CTests **4/4** in 3.71 seconds, including the wrong-colony pending-rescue isolation assertion. Changes after this code commit only update this engineering document; broader PR workflows remain separate.
