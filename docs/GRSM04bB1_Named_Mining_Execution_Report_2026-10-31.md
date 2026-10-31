# Single Galaxy — GRSM-04b.B1: Named NPC Mining Execution

**Date:** 2026-10-31  
**Status:** GRSM-04b.B1 Linux acceptance passed (GRSM-04b.B2 remains open).  
**Baseline:** GRSM-04b.A (SQLite v15), local authoritative C++ world and SQLite.

## Why this gate exists

A committed ship-level resource movement is not sufficient evidence that any member of the crew personally performed the work. GRSM-04b.A deliberately proved only named casualties and firsthand witnesses. This gate introduces the **first explicitly scheduled NPC job with a real executed resource effect**, so the game can distinguish (a) being aboard a ship, (b) accepting a particular worker's assignment, and (c) actually completing a mining command that changes the physical world.

## Implemented

- SQLite schema **v16**, migrating old databases without inventing any historic worker assignments or executions.
- `PersistentWorld::submit_npc_mining_work(npc_id, ship, rock, amount, command_id)` is a trusted host-only work submission route. A named worker must exist as a surviving individual, assigned to the mining ship and located aboard it, with the `Ship` workforce role. The authoritative simulation accepts an ordinary owner-authorized `Action::Mine` command. Remote crew, nonexistent workers, invalid amounts and duplicate command IDs are rejected.
- An append-only **assignment receipt** is created in the same SQLite transaction as the accepted command, keyed to its real command identity and assigned source/target.
- At the actual simulation step, the same transaction that commits the result, resource ledger, physical `CargoMovement` incident and world snapshot can also create a distinct **named execution receipt**, but only for `Outcome::Applied` with positive measured ore transfer, matching source and destination, matching assigned command ID and a still-present worker aboard the ship.
- The successful case exposes `recent_npc_mining_assignments`, `recent_npc_mining_executions` and `verified_npc_mining_execution` read-only queries. Generic ship commands and failed/empty named commands never become evidence of individual work.
- Restore rejects broken sequence/digest tips, altered worker identity/amount/physical source, missing rows, missing original accepted command and contradictory executed receipt. `recover_copy` retains verified assignments and executed work. These are **coding-error / ordinary-tamper checks**, not cryptographic proof against a fully privileged database editor.
- The existing original Single Galaxy resource conservation and server-only world authority remain in force. No client or local web relay is granted new NPC executor privileges.

## Verification results

- The dedicated named-executor suite includes two scenarios: (1) physically applied mining, ordinary ship-only work, failed command, replay, injected rollback, restart/recovery, and deliberate source tampering; (2) v15-to-v16 migration with no invented worker claims and a new successful job after migration.
- All 13 persistence/GRSM suites passed after fixing older migration-fixture expectations to v16.
- JavaScript viewer/protocol tests: **84/84** passed.
- Design unit tests: **3/3** passed.
- First full-target compile was interrupted by the execution window at 77/106 steps; resumed incremental build finished successfully.
- **Final bounded, complete Linux run: 66/66 native and integration tests passed.** Earlier whole-suite runs were interrupted at 61/66 and at an older relay scenario; these traces are preserved as non-complete evidence. The isolated B65 gate relay passed, as did the final relay tail group. The full final run used bounded per-test timeout and included all legacy live-host tests.

## Limits and next gates

1. **B1 is deliberately narrow**: the engine has a trusted host job route for named **mining**, not an autonomous labor scheduler, NPC-owned decisions, or a universal actor identity for every C++ command. Actor declaration is authorized by the host pathway and tied to actual committed effects, but it is not a separate physical witness that the NPC personally swung a tool.
2. A failed mining job retains an immutable assignment and failure outcome, without executor credit. When a worker is absent at execution, the simulation's existing ship command may still complete, but it cannot create a named worker-execution receipt.
3. One worker-to-one-command attribution is supported. Joint work, labor hours, training, performance, wages, sabotage, equipment provenance, order-giver versus executor versus beneficiary, and NPC consent/refusal remain separate future gates.
4. Reputation attribution, guilt, criminal culpability and group sanctions are **not inferred** by this receipt alone. Social opinions may dispute the motive or fairness of a legitimate action without contradicting what cargo physically moved.
5. The deferred **binary multiplayer protocol** and **dual-mode launcher/server administrator GUI** are documented separately as NET-01 and intentionally not implemented in this gate.
6. Windows-native end-to-end, GUI game validation and large-scale population benchmarks remain open.

**Next:** GRSM-04b.B2 — broaden committed named jobs (e.g. authorized farm work, material transfer or repair), establish who gave orders versus who executed, and introduce reusable actor-bound effect receipts without assuming ordinary proximity equals responsibility.
