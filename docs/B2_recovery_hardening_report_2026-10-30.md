# Single Galaxy — B2 Recovery Hardening and Native-Windows Preparation

**Date:** 2026-10-30  
**Project:** New original single-galaxy simulator, starting in just one headless star system.  
**Status:** **Linux-verified implementation; Windows-native build and filesystem tests NOT performed.**  
**Boundary:** No EVEmu code, game client or asset used; no GitHub operations.

## Changes actually implemented

1. **Atomic schema upgrade:** SQLite `user_version=1` → `2`, adding `operation_log`. Existing v1 saves load and are preserved in the migration test. Each new tick and submitted command is journaled in the SAME SQL transaction as the snapshot and receipt changes. Old v1 pre-migration actions are NOT retroactively reconstructable.
2. **Replayable committed operations:** New submission records serialize the complete command; step records store the resulting tick and FNV-1a state hash. Recovery requires a complete contiguous sequence, checks the hash and total ore at EVERY replayed generation, and reconciles receipts and the resource ledger after construction.
3. **Explicit recovery into NEW database:** `galaxy_recover_copy <source> <unused-destination>` opens original read-only, uses SQLite online backup (WAL-aware) for a consistent copy, then replays the mutation journal from a retained verified checkpoint on the copy only. If there is any missing operation, mismatched checksum, malformed saved command, ledger conflict or receipt mismatch, recovery refuses and deletes the attempted destination. The original is left intact. Recovery is NEVER automatic on game load.
4. **Ledger validation on restore:** A genesis entry and all mining-transfer entries are checked against restored executed events. Resource-transfer tampering is rejected.
5. **Pinned reproducible SQLite packaging path:** On Windows, `scripts/prepare_sqlite.py` accepts either an official downloaded ZIP or automatically retrieves it, verifies SHA3-256, then extracts the two upstream amalgamation files. If present, CMake compiles them statically. Otherwise, system SQLite SDK remains required. **The amalgamation is NOT bundled in the ZIP** (offline sandbox download unavailable), and this new vendored compilation path is not yet tested.
6. **Windows smoke script:** `scripts/build_and_test_windows.cmd` now runs CTest and `windows_process_smoke.cmd` for normal and abrupt-exit process restart; the script itself has not been executed on Windows.

## Linux verification

| Check | Result |
|---|---|
| Original deterministic kernel tests | 10/10 pass |
| Expanded SQLite persistence suite | 21/21 pass (13 pre-existing + 8 new) |
| GCC 14.2 Release, CTest | 2/2 CTest targets pass |
| Clang 17 Release, CTest | 2/2 CTest targets pass |
| Clang 17 Debug + UBSan, CTest | 2/2 CTest targets pass |
| Corrupt committed submit snapshot, recover to copy, then execute pending mining in a separate process | PASS: ship cargo 12; asteroid 68; total ore 80 |
| Migration from a previously populated v1 DB, then two additional v2 operations | PASS |
| Corrupted latest + corrupted penultimate snapshot with a valid older checkpoint | PASS: verified replay to latest generation |
| Missing journal operation, contradictory receipt, ledger tampering | PASS: fail closed |
| Existing recovery destination | PASS: refuses overwrite |
| Injected pre-commit failure | PASS: no orphan operation-log row |

**New tests added:** v1 migration; copy recovery with source unchanged; recovery from second retained checkpoint; missing-journal rejection; tampered-receipt rejection; existing destination refusal; operation-log atomicity; ledger tamper rejection.

## Early persistence-only Linux benchmark (not an NPC or networking benchmark)

A single sampled 100-tick run measured `PersistentWorld::step()` (full-state snapshot and journal commit, SQLite WAL, `synchronous=FULL`); loopback local temp filesystem, no market, no contention:

| Asteroids | Steps | median commit+tick | p95 | p99 | max | elapsed incl. init |
|---:|---:|---:|---:|---:|---:|---:|
| 100 | 100 | 1.219 ms | 2.923 ms | 3.366 ms | 3.421 ms | 152 ms |
| 1,000 | 100 | 2.981 ms | 8.769 ms | 21.009 ms | 32.956 ms | 418 ms |

These are **one-run exploratory measurements**, not reproducible device-independent performance claims. File sync and storage are highly environment-dependent; the current engine copies the entire world and rewrites snapshots each tick. No throughput/latency suitability claim for large galaxies is warranted.

## Important limits and remaining gates

- **Windows and source-bundled SQLite compilation are NOT verified.** The archive-fetch helper needs a separate online Windows/Linux run using the pinned official archive, then the Windows build and smoke script must run on a real Windows host.
- FNV-1a state hash is accidental-corruption detection, not cryptographic evidence. The recovery process is not suitable for adversarially edited SQLite saves or malicious journal data.
- Recovery is available only when retained checkpoint(s) plus *complete v2* operations exist. Older v1 operations cannot be replayed. If any critical history is missing, fail closed.
- The recovery utility must be invoked explicitly by an operator who preserves the original. The new copy should be validated before use; no tool overwrites the original.
- Full-state snapshots, accepted IDs, receipts, event history, and operation log grow unbounded. Implement safe snapshot/receipt/event compaction before long-running gameplay.
- No true power-cut / disk torn-write / Windows AV-interference / write-while-recovering testing. A commit succeeds before process termination injection; no mid-COMMIT crash simulation.
- No NPCs, market, docking, full 3D physics, networking, graphics or multiple active star systems have been implemented.

## Next recommended gate

**B2-WIN:** Download and hash-check SQLite amalgamation on the Windows workstation, run `scripts/build_and_test_windows.cmd`, preserve logs of native x64 build/tests and run two separate-process recovery experiments. Then add command history bounds and checkpoint compaction **before** expanding gameplay to B3 NPC economy.

## Windows developer quick-start

```bat
py -3 scripts\prepare_sqlite.py
scripts\build_and_test_windows.cmd
```

For offline development, download `sqlite-amalgamation-3540100.zip` from sqlite.org in advance and run `py -3 scripts\prepare_sqlite.py --zip C:\path\to\sqlite-amalgamation-3540100.zip`.

Official SQLite home/download and amalgamation documentation: https://sqlite.org/download.html ; https://sqlite.org/amalgamation.html
