# B2 — Persistence & Rollback Verification Report
**Date:** 2026-10-30  
**Project:** Single Galaxy original independent backend  
**Runtime:** local Linux x86-64 sandbox. No Windows validation, EVEmu modification, client inspection or GitHub push.

## Achieved features by B2 proposal

| Task | Result | Evidence / qualification |
|---|---|---|
| B2.1 SQLite/schema | **PARTIAL** | Database bootstrap, `user_version=1`, unknown versions rejected; automatic schema upgrade migrations and bundled Windows SQLite still missing |
| B2.2 State restart | **PASS (Linux)** | Full versioned snapshot of tick, entity IDs, RNG state, entities, pending commands, accepted IDs/events; tested restore + deterministic replay |
| B2.3 Durable command receipts | **PASS (small scope)** | SQLite `receipts.id PRIMARY KEY`, snapshot cross-check; accepted commands remain rejected across restart |
| B2.4 Staged atomic publish | **PASS (injection)** | Copy world, mutate stage, write receipt/snapshot/ledger transactionally, `COMMIT`, then `noexcept` swap; injections before COMMIT leave live/DB unchanged |
| B2.5 Snapshot validation | **PARTIAL** | `galaxy-kernel-v1` numeric format, size limit, FNV checksum, typed invariants, 3 retained checkpoints; newest corrupt save rejected, no automatic rollback/replay |
| B2.6 Recovery | **PARTIAL** | Submit persisted in separate process, second process executes; abrupt post-commit exit test passed; no real power-cut / mid-COMMIT test |
| B2.7 Resource ledger | **PARTIAL** | Initial resource-total entry, per-mining-transfer entries, invariant `sum(asteroid.ore + ship.cargo) = initial total`; no intentional sinks/production in B2 |
| B2.8 Windows | **NOT TESTED** | Windows script included; SQLite SDK not bundled; native MSVC build/runtime pending |
| B2.9 Stress | **SMOKE PASS** | 250 commands+ticks, restart and duplicate rejection; not real high-load benchmark |
| B2.10 Diagnostics | **PARTIAL** | Explicit error causes for checksum, failed SQL, schema, generation, receipt mismatch; no structured telemetry/performance counters |

## Test suite

Original `SimulationTests.cpp`: **10/10 passed**.  
New `PersistenceTests.cpp`: **13/13 passed**:

1. `fresh_database_and_restore`
2. `pending_command_survives_restart`
3. `failed_submit_never_publishes`
4. `failed_step_rolls_back_everything`
5. `deterministic_replay_after_restore`
6. `reject_corrupt_latest_save`
7. `reject_future_schema`
8. `reject_tampered_receipts`
9. `reject_tampered_receipt_identity`
10. `reject_stale_writer`
11. `rejected_command_is_durable`
12. `many_command_ids_and_resource_conservation`
13. `reject_unauthorized_resource_creation_post_initialization`

**Compilers/tests run:**

- GCC 14.2 Release — both CTest targets pass.
- Clang 17 Release — both CTest targets pass.
- Clang 17 Debug with `-fsanitize=undefined -fno-sanitize-recover=all` — both CTest targets pass.

## Distinct executable-process restart evidence

Normal shutdown case:

```text
PROCESS 1: galaxy_persistence_demo <newfile> init
INIT saved pending command=1; tick=0

galaxy_persistence_demo <samefile> resume
RESUME mined=12; cargo=12; asteroid=68; conserved=80

PROCESS 3: galaxy_persistence_demo <samefile> resume
FAIL resume expects pending command at tick=0
exit status=1 (expected rejection)
```

Abrupt process termination **after the submit COMMIT** and without executing destructors:

```text
PROCESS 1: galaxy_persistence_demo <newfile> crash-init
INIT saved pending command=1; tick=0
INJECT abrupt process termination after committed submit
exit status=77 (intentional termination)

PROCESS 2: galaxy_persistence_demo <samefile> resume
RESUME mined=12; cargo=12; asteroid=68; conserved=80
```

The parent program runs a distinct process for each command. This does NOT simulate a torn filesystem write or a mid-commit power outage.

## Transaction behavior

- `submit` persists acceptance before it returns success; pending accepted commands survive restart.
- `step` persists the complete computed state and updates receipt outcomes in the **same** SQLite transaction.
- A successful mining operation is recorded in the `resource_ledger` with source/destination IDs, tick and amount.
- If an injected error occurs after receipt changes or after snapshot insertion but **before COMMIT**, SQL rollback undoes those changes. No in-memory mutation is published.
- `BEGIN IMMEDIATE` serializes writers; stale sessions cannot overwrite a newer generation.
- Latest snapshot corruption fails closed. Older checkpoints are retained but **not automatically restored**, because their command ledger may reflect later committed operations. A safe recovery mechanism must reconcile or replay all subsequent commits.

## Architectural limitations requiring next gates

1. Implement schema migration from version 1 to future versions; adopt/test packaged SQLite amalgamation or other reproducible vendor strategy for Windows.
2. Replace full-state snapshot rewriting + unbounded receipt/event growth with journal/snapshot compaction and bounded recovery; preserve exactly-once semantics.
3. Validate actual process/power interruption mid-transaction on multiple filesystems and Windows.
4. Implement explicit stock creation/destruction cause codes and economic ledger for NPC production, consumption, trade, crafting and salvaging.
5. Instrument baseline memory, transaction throughput, latency distributions, simulation tick and restore time.
6. Add a deliberate recovery utility for corrupted latest snapshots; fail closed until safe replay and reconciliation are demonstrated.
7. Add native Windows x86-64 tests and launcher packaging. No Windows claims should be inferred from Linux results.

**Conclusion:** One tiny star-system simulation now durably preserves accepted commands, movement and resource transfers across SQLite close/reopen, detects common corruption, and remains consistent under injected pre-commit failures. This is a substantial B2 prototype gate, with several B2 tasks explicitly partial or deferred.
