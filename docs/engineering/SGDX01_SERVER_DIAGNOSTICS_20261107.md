# SGDX — Native server diagnostics and reproducible evidence

Status: SGDX-01 native component PASS; full game-host integration OPEN. Owner requested server DX/crash evidence after the INT64_MIN sector-coordinate bug was reproduced. Applies to the original Single-Galaxy game, not EVEmu.

## SGDX-01 — Local exception incidents (implemented)

- The original single PersistentWorld SQLite writer remains authoritative and untouched. Diagnostics are local sidecars; no secondary game ledger.
- ServerDiagnostics.hpp emits private SGDX-01 JSON on unhandled C++ exceptions in colony_live_host and interactive_host.
- Schema: epoch_ms, host, phase, last_committed_tick, last_world_generation, bounded exception message. Recorded committed tick is not an assertion that a failed step committed.
- Path: the game-save filename plus ".diagnostics" directory; private directory and files, reject directory symlinks, temporary file then rename, capped at 16 incident JSON files.
- Nothing is automatically transmitted or placed in SGVIEW, owner-private SSE, or normal player telemetry. Exception messages can contain sensitive details; explicit review/redaction before exporting.
- Best effort only: a diagnostics write failure cannot mask the original host failure or modify game state.
- Executed isolated native Linux CTest using exact committed source blobs: GCC 14.2 Release PASS; Clang 17 Release PASS; Clang 17 UBSan PASS (3/3). Source hash identity checked.
- Registered genuine two-native-host failure-injection CTest: invalid tick interval, no client frames, no accidental game-save creation, one private startup incident for each host. Source submitted; whole-game CMake execution still OPEN.

## SGDX-02 — OS crash dumps and worker supervisor (OPEN)

SGDX-01 cannot catch SIGSEGV, access violations, abort(), forced worker termination, OOM kill, or a hard machine failure.

Build a separate parent-process diagnostic supervisor:
1. Linux: process exit/signal plus opt-in operating-system core collection (when permitted); retain bounded pre-crash stderr without unsafe signal-handler file I/O.
2. Windows: separate DbgHelp minidump capture with build/PDB IDs and Job Object process-tree outcomes; dumps may contain personal data and must never upload automatically.
3. Correlate source commit/build, host role, worker generation, cell scope, dispatch/command receipt, last successful SQLite generation, worker exit state, and crash time.
4. Private, capped local storage with explicit redacted export and integrity hashes; independent no-dump tests for permissions, disk full, crash loop, and symlink attempts.
5. Test actual forced process death during durable work and verify restart with no duplicate physical effects.

## SGDX-03 — Cross-system original-world integration (NEXT TEST GROUP; OPEN)

- Native galaxy physical star-label/cell boundary crossing: verify real 3D distance, velocity, docking and real original-world receipts.
- Passenger/rescue: named NPC and voyage conservation during source ferry emergency rescue, independent SQLite recovery, no imaginary replacement crew.
- Freight: source debit/destination credit conserve finite mass under failure/recovery; physical rendezvous alone never authorizes stock mutation.
- Combat/ship destruction: stale commands cannot move inventory/passengers from a destroyed carrier; no double-applied receipts.
- Failure injection before and after snapshot write; keep host incident, CTest JUnit, exact commit, and recovery state.
- Keep a full installed-world sustained performance test distinct from isolated worker speed.

Existing source coverage includes tests/V59_cross_system.cpp, tests/FLIGHT05_PhysicalRescueTests.cpp, tests/FLIGHT06_DependentRendezvousRescueTests.cpp and tests/NativeGalacticProbeSupervisorTests.cpp. These are not proof of an exact-head full matrix.

## Pattern-breaker acceptance rule

After repeated no-runner failures, do not keep retrying the same full checkout or assert 94 failures reproduced. Pivot to another independently executable source-level test, commit only evidenced fixes, and preserve blocked whole-project acceptance gates. Full clean Linux matrix remains required before 1,024-tick four-cell acceptance. Multiplayer networking is deferred by owner.
