# COLONY-AUTO-04D.5A — Host-supervised NPC emergency coordination (v78)

## Goal and boundary

An NPC coordinator can initiate an actual, preexisting **whole-family evacuation** without the player selecting a rescue ship. The system does not assume an omniscient AI, invent unobserved danger, create NPCs, conjure provisions, or weaken legal custody. This v78 gate is intentionally a local, high-confidence emergency **dispatch**. Repair execution, supply hauling, emergency transportation across regions, NPC manager selection and unattended recurring scheduling are future gates.

## Trigger: delivered, grounded knowledge rather than speculation

`colony_safety::journal::record_world_commit` persists the physical life-support risk transition; `colony_safety::notice::publish` records the local named-NPC opportunity; trusted host `acknowledge_colony_notification` writes the actual delivery. A coordinator can only act when there is a delivered acknowledgement for the **most recent** source-colony response event, its original physical compartment equals the coordinator's current location/home, and the currently recomputed risk is exactly **Critical** or **Emergency**. An earlier forecast, pending message, another NPC's observation, or recovered stale warning does not authorize departure.

`PersistentWorld::assess_autonomous_colony_emergency` returns the normal `ResponsePlan` plus an optional candidate `AtomicColonyFamilyPlan`, a verified notification flag, and a refusal reason. Its decision is read-only. The trusted runtime supplies the NPC identity and stable intent/command/event IDs, registered family and region scopes, and the host's existing real ship→regional-colony bindings. These are typed authority inputs, not an unrestricted player-facing decision hook.

## Deterministic choice

The controller derives all adults from the current `Simulation::npc_records()` at the source and all already physically registered children from `physical_dependents()`. The guardian ID is taken from the **current physical custody record**; callers cannot change it in a plan. The result is verified by the existing cross-ledger `colony_family::assess`, which checks actual ART02 births and legal guardians, full source population, physically reachable assignments, same owner and system, available physical berths, and recipient life support. The controller separately reconciles the source and candidate ART02 census against the cold ECON03 regional checkpoint, checks the owner is stable/cold, and requires **four future real food, water and oxygen consumption periods** at the receiving ship (stricter than the two-period native boarding minimum).

Among qualified host-bound candidates it picks minimum physical squared distance to source, then smallest ship ID on ties. No qualified candidate means **no evacuation**, not a fictional rescue or an exemption from the emergency/distress policy. A nearby ship in another family scope, at a stale regional revision, with insufficient supplies, or unreachable by crew must be rejected.

## Admission vs execution

`PersistentWorld::submit_autonomous_colony_rescue` calls the verified read-only selection, performs the existing generation-bound immutable `prepare_host_colony_family_departure`, and then writes the *accepted* host departure command and immutable `sg_colony_npc_autonomous_dispatch` receipt in **one existing `Store::commit` transaction**. Under the writer lock it independently checks the original prepared family, current delivered notice and stable cold regional owner/revision. Command admission is an explicit, durable record of the actual NPC-initiated **intent**, not an evacuation result.

Only `PersistentWorld::step_with_atomic_family_departure(decision.transfer)` can actually move anyone. The full v76 physical world, ART02 residence and ECON03 population transaction remains unchanged, with the same all-or-nothing crash recovery and full child guardian custody proof. Separately persisting preparation is safe: preparation relocates nobody and can be idempotently retried if admission fails. Repeat dispatch for a pending/consumed command fails.

The v78 query `Store::verify_autonomous_dispatch` independently reconstructs the originating accepted world snapshot, `operation_log` kind and state hash, originally prepared manifest, NPC's locally present identity, current shortage at admission, actual delivered warning and its original recency. Immutable update/delete triggers guard the autonomous receipt. The audit is called on normal `PersistentWorld` startup, independent `recover_copy`, and positive evacuation-status queries. Corrupt records fail closed.

## CI acceptance matrix

`tests/COLONY_AUTO04D5A_AutonomousEmergencyTests.cpp` creates **three real colony ships**: one running critically short of food/water/oxygen, and two stocked nearby receivers with correct regional checkpoints. A real ART02 birth and physical child admission precede the observed shortage. The host has to deliver the shortage event to a real source NPC; an undelivered opportunity is refused. The controller autonomously picks the nearer ship regardless of candidate order and binds the complete child/parent roster and current region revision. Duplicate IDs, forged warning and repeat dispatch are refused.

After injecting a failure after admission snapshot write, world generation, command ledger, child occupancy and regional population remain unchanged. A retry succeeds, but does not move anyone until the existing v76 execution gate runs; that one execution moves both parents and child, with receiver needs consuming all five people, conserved named NPCs and real material. Restart and independent recovery verify the actual decision chain. Privileged tampering with a persisted leader and its immutability guard is detected at restart.

## Remaining work

This is the **first autonomous decision/action bridge**. It depends on the trusted host invoking this coordinator with an actual designated NPC and actual destination-region bindings; there is not yet a continuously scheduled NPC manager loop. The runtime should later integrate the same evidence with staff/repair/supply tasks and escalate distress when physical rescue is impossible. A two-step preparation-and-admission intent should not be represented to players as an executed rescue.

**Focused acceptance verified:** [GitHub Actions run 41773148442](https://github.com/vjmichael/Single-galaxy/actions/runs/41773148442) at commit `284420c6c8d1a1ae9a9b6614a6030aab336f60c6`. All **3/3** actual CTests passed: v76 atomic family custody, v77 mixed parent-guardian fault matrix, and v78 observed NPC emergency dispatch with crash/tamper/recovery. The follow-on change to this document does not change executable code.

**Production handoff still needed:** The trusted host currently supplies a fresh bounded command/event-ID allocation and calls the coordinator. A general always-on NPC supervisor must schedule and retry these calls; a crash between command admission and its due execution still requires the host to retain/recover the full v76 transfer plan, not just the admitted command ID. This remains an explicit resilience gate, not a claim of a complete unattended emergency scheduler. Keep PR draft until broader matrix and integration review.
