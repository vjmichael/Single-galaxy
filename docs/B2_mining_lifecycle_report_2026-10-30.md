# Single Galaxy — B2 Mining Lifecycle Engineering Report

**Date:** October 30, 2026  
**Status:** Implemented and tested on Linux; Windows pending  
**Baseline:** independent B2 recovery-hardened C++17 + SQLite prototype; separate source copy maintained

## Purpose

The user's two original reproduction cases identified a zero-ore asteroid staying registered, and a missing repeating mining cycle. Both were addressed in the actual simulation kernel rather than being simulated by a stand-alone fixture.

## Results

| Scenario | Input | Tested result |
|---|---|---|
| Ten asteroid depletion | 10 rocks x 10 ore, ship capacity 100, sequential one-off mining | 10/10 rocks removed from `entities()`; 10 depletion events; ship 100/100; total 100 conserved; reopen PASS |
| Automatic full hold | One rock 20 ore, ship capacity 10, continuous cycle 4/tick | 4+4+2 mined by tick 3, `MiningStopped(CARGO_FULL)` on tick 3; subsequent ticks idle; ship 10/10, rock 10; reopen PASS |
| Single-shot mining fills hold | Rock 20, ship capacity 10, one-shot mine of 20 | 10 transferred, immediate full-hold activity; subsequent command returns `CARGO_FULL` |
| Auto depletion | Rock 10, capacity 100, cycle 4/tick | 4+4+2, rock despawns, job stops with `NO_RESOURCES`, total ore 10 |
| Restart of active job | Rock 80, capacity 12, cycle 3/tick | First cycle before restart; cycles after reopen complete to 12 and stop |
| Concurrent mining | Two ships cycle on same 10-ore rock at 7/tick | First receives 7, second receives 3, asteroid despawns and both jobs stop during same tick |
| Ship exits range | Moving ship loses mining range | Auto job stops with `OUT_OF_RANGE`, no further ore transferred |
| Manual stop | Stop command issued while job active | Job stops, no additional mining on later ticks |
| Failure injection | SQLITE exception after receipt or snapshot writes | Database and in-memory world remain unchanged; retry succeeds |
| Corrupt newest snapshot | Tamper latest state blob | Recovery to a separate DB copy via verified replay; source remains rejected and untouched |
| Tampered auto resource ledger | Corrupt transfer amount or source ID | Restore refused |
| Legacy empty rock fixture | Previously saved B2 world with 10 zero-ore asteroids | Actual old database copy loaded; next tick removes all 10, preserving 100 ore |

## Commands and data structures

- `Action::StartMining` / `Action::StopMining` complement the original one-shot `Action::Mine` and `Action::Thrust`.
- Active jobs are keyed by ship ID and carry `target`, `amount_per_tick` and the initiating command ID.
- `ActivityEvent` records `AsteroidDespawned`, `AutoMiningTransfer` and `MiningStopped`, including stop reason. These are separate from original one-event-per-command receipts.
- `process_automatic_mining` processes actor IDs in deterministic sorted order; destruction of a rock immediately cancels other active jobs pointing at it.
- Depletion tombstones maintain stable identifiers without keeping empty asteroids in the active-world entity collection.
- SQLite ledger uses `automated_mining_transfer` entries with source/destination identifiers and amount. Restore validates the count, ordering, amount, tick and entity IDs against the snapshot's activity events.
- Snapshot header automatically transitions from `galaxy-kernel-v1` to `galaxy-kernel-v2` when new lifecycle state is needed; older snapshots are still parsed as v1. The SQLite `user_version` remains 2.

## Verification record

- Linux GCC debug: `ctest`: **3/3 passed**.
- Legacy focused suites: B1 **10/10**, B2 **21/21**.
- New lifecycle suite: **15/15**. Combined: **46/46 focused checks**.
- Linux Clang 17 `-fsanitize=undefined -fno-sanitize-recover=all`: **3/3 suites passed**.
- Linux Clang 17 `-fsanitize=address` with leak detection: **3/3 suites passed**.
- Previously generated actual B2 SQLite save copied to `/tmp` (original untouched); 11 active entities and 100 ore before first tick, 1 active entity and 100 ore afterward: PASS.

## Remaining gates

1. Test on native Windows x86-64 against a pinned, vetted SQLite amalgamation; verify native file locking and restart behaviors.
2. Add periodic snapshots / compaction to cap event and depleted-ID growth and make world load times bounded.
3. Verify COMMIT-to-memory-publish crashes and disk-failure recovery on durable storage.
4. Build tick scheduling/worker lifecycle and a graphical client event-consumption adapter.
5. Add mining cycles with cooldown and energy requirements, cargo unloading and the NPC economy only after new tests pass.
6. Stress many simultaneous miners / asteroids and measure tick time, allocations and SQLite I/O under realistic workloads.

## Scope and provenance

Original-game code only; not EVE-compatible and no proprietary client/assets included. This report covers executable tests in the provided Linux environment, **not** Windows compatibility, 3D simulation, networking or a complete game. No GitHub push or external repo changes.
