# SGDX-02 — Native server crash/worker supervisor and incident evidence

**Date:** 2026-11-07 / 2026-11-08 UTC. **Target:** original Single-Galaxy C++/SQLite game, PR #95, stacked draft branch `space/worker-io-deadline-20261107`.

**Status:** Linux standalone supervisor **native component verified**. Full original game-host acceptance, real internally spawned worker manager integration, crash-time Windows minidumps, and Windows installed-app acceptance remain **OPEN**. Owner directed SGDX-02 before any new gameplay test group; **do not proceed to cross-system integration tests until the required DX gates are met**.

## Purpose

The independently supervised child process may crash before executing its own C++ exception handler. A good failure receipt must explain process/worker exit independently, preserve the last host-acknowledged committed state, and not corrupt the original game, leak another player's state, or block gameplay while recording evidence.

## Implemented source

- `src/server_dx_supervisor.cpp` is an independent native C++17 executable. Launch using `galaxy_server_dx_supervisor --save SAVE.sqlite --role colony_live_host --build-sha <40-hex> --timeout-ms 60000 -- galaxy_colony_live_host SAVE.sqlite 20 1200`. The build SHA is caller-attested; the helper does not cryptographically verify a release binary.
- Linux: fork/exec child in its own process group, original stdout inherited **unchanged**, capped 8 KiB stderr tail retained only in local incident evidence. The supervisor distinguishes clean exit, nonzero exit, OS signal, timeout and operator cancellation; kills the process group on timeout or after leader exit. A process that deliberately detaches using a fresh session requires stronger cgroup enforcement later.
- Linux host/worker breadcrumbs: `include/galaxy/ServerBreadcrumb.hpp` writes short nonblocking `COMMIT|tick|generation` messages over an inherited private AF_UNIX datagram socket, with `MSG_NOSIGNAL` to prevent a dead supervisor killing its child via SIGPIPE. Only emit after an original `PersistentWorld::step()` returns successfully and when loading an existing committed world. These are **last observed** ticks, not proof that no later SQLite commit occurred.
- Linux core memory collection is OFF by default (`RLIMIT_CORE=0`). Explicit `--allow-core` caps at 64 MiB; OS core location and actual collection remain OS-policy-dependent and are **not verified** by this component test. Never upload a core file automatically: it may contain credentials and sensitive game/player data.
- Windows source branch: `CreateProcessW` starts suspended, assigns a kill-on-close Job Object, preserves inherited standard handles (not synthetic game frames), then resumes; nonzero and timeout process metadata are recorded. Windows `--allow-core` is deliberately rejected. Windows minidump capture, native ACL restrictions and committed-tick IPC are **not implemented**; do not claim Windows production DX acceptance.
- `include/galaxy/ServerProcessDiagnostics.hpp` stores best-effort `SGDX-02` process JSON sidecars beside each save (separate from game SQLite and SGDX-01 exception incidents). Fields: event time, host role, caller-declared build SHA, reason, PID, numeric exit/signal, elapsed ms, last observed committed tick/generation, accepted breadcrumb count, core opt-in flag and capped stderr tail. Create temp file, rename and retain at most 16 process incidents. A symlinked incident directory is rejected. Owner-only POSIX mode is applied. Exceptions / write errors in diagnostics must not falsely mark a failed child successful.
- Source hooks: `src/colony_live_host.cpp` and `src/interactive_host.cpp` produce private post-commit breadcrumbs. The original SQLite single-writer and all physical game rules remain unchanged.
- `CMakeLists.txt` registers a native supervisor target, native fault child, `galaxy_server_dx_supervisor` component CTest and `galaxy_server_dx_game_hosts` integration CTest. Separate `tests/sgdx02/CMakeLists.txt` compiles only the supervisor and fault child independently of the full game, letting DX tests advance even while game-wide checkout/runner provisioning is blocked.
- `.github/workflows/sgdx02-native-supervisor.yml` requests independent Linux and Windows runner tests and JUnit artifact preservation.

## Real native Linux standalone acceptance

A complete isolated CMake/Ninja Release build was run with the exact committed SGDX-02 component source blobs. Three separate native runs passed:
- GCC 14.2: CTest 1/1 PASS.
- Clang 17: CTest 1/1 PASS.
- Clang 17 with UndefinedBehaviorSanitizer: CTest 1/1 PASS. The sanitizer may intercept a SIGSEGV and exit nonzero itself; the test verifies that trace rather than inventing a raw kernel signal.
- The Python test launches a real compiled C++ child repeatedly: clean SGVIEW stdout, nonzero exit, actual SIGSEGV and SIGABRT (on Linux), deliberate hung child, 40 KiB stderr burst with 8 KiB tail, **invalid UTF-8/NUL stderr that still produces parseable ASCII-safe JSON**, two successful commit breadcrumbs before fatal exit, descendant process termination on timeout, sixteen-entry incident rotation, private POSIX permissions, symlink refusal, caller build SHA and explicit clean-exit recording. Result is real subprocess behavior, not mocked JSON.
- Native source Git blob hashes were compared against the GitHub committed files before acceptance. An evidence ZIP containing exact source, JUnit, compiler/CTest logs and SHA-256 manifest is supplied in conversation.

## Exact game/native and Windows gates (not accepted)

1. **Original hosts:** `tests/SGDX02GameHostTests.py` must be executed against original compiled `galaxy_colony_live_host`, `galaxy_interactive_host` and `galaxy_native_cell_worker`. It checks dual SGDX-01 + SGDX-02 incident receipts on real invalid startups, actual player-output isolation, successful 5-tick persisted SQLite worlds, and correct last-observed tick receipts on Linux. Registered in CMake, **not executed** due inaccessible full exact-head game checkout and GitHub runners.
2. **Original active worker dispatcher:** route actual native cell worker spawns through the supervised path without weakening fencing, authorization, cancellation, owner generation, or durable physical journal recovery. The separate SGDX-02 wrapper does **not** automatically supervise all already-existing worker manager calls.
3. **Windows:** prove Job Object child-tree teardown in real native run; private ACLs and external crash-time dump capture using a trusted debugger/WER or `MiniDumpWriteDump` **before process exit**, with explicit local opt-in, strictly capped retention, no automatic upload and reproducible PDB/build IDs. A dump attempted after a dead process exits is not acceptable evidence.
4. **Linux hardening:** verify no escapes for deliberate detached descendants using cgroup when promised, force kill/restart during SQLite transaction, test malformed / missing breadcrumbs, disk-full and permissions faults, sanitization of stderr and leakage controls, report persistence after abrupt supervisor death, and measure host overhead / longest stall.
5. **Whole project:** complete clean exact-head Linux CMake/CTest and Windows compilation still required, then 1,024-tick four-cell acceptance; the historical 94 failures are not reproduced on this SGDX-02 source head.

## Pattern-breaker rule

Do not rerun an infrastructure no-runner failure endlessly. The diagnostic component has independently executed proof; retain every broader full-game acceptance gap as OPEN and move neither to multiplayer (deferred by owner) nor to the cross-system test group before SGDX-02's integration obligations are addressed.
