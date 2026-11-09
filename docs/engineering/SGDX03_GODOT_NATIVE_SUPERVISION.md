# SGDX-03 — Supervised Godot → original C++ simulation and SQLite
**Status: implemented on draft PR #125; exact-head Windows/Linux runtime gates OPEN.**

## Runtime topology
The Godot engine remains visual and input-only. For P ship, H station, and O telemetry sessions, `native_*_session.gd` calls `sgdx_process_launch.gd`, which **requires** a bundled `galaxy_server_dx_supervisor(.exe)`. No direct native-host launch fallback exists. The host receives its established `SQLITE_PATH` and session flags unchanged through a genuine separate executable. Launch uses `OS.execute_with_pipe(supervisor, args, false)` with **no shell**. All existing POSE/SHIP/ROCK/ROCKNONE/ACK/DENY/REF* and movement commands remain byte-for-byte C++ messages; the debugger writes **nothing** to game stdout and must never create a second world writer.

- P original cockpit: `--save <path> --role godot_ship --interactive --record-clean -- galaxy_desktop_player_host <path>`.
- H original station: `--save <path> --role godot_station --interactive --record-clean -- galaxy_desktop_station_host <path> --explicit-test-station`.
- O original bounded observer: `--save <path> --role godot_observer --interactive --record-clean -- galaxy_desktop_readonly_host <path> 600`.
- An optional pinned `SGDX_BUILD_SHA.txt` in the same native folder supplies `--build-sha` to SGDX. The actual compiled, packaged Windows build **must** ship that manifest.
- An interactive supervisor has no arbitrary one-hour wall clock expiry. It still owns child custody and failure handling. Windows uses a kill-on-close Job Object and inherited limited stdio handles; Linux uses a child process group and parent-death signal. SGDX is not a second command authorizer: C++ still validates user/session/ship, performs durable STEP and holds the sole original SQLite writer lease.
- Normal ship/station handoffs send original `QUIT`, await supervisor exit before a replacement may open SQLite, and allow an SGDX `clean_exit` sidecar. On malformed frames or timeouts, Godot kills the SGDX supervisor to close its custody tree; there is no claim that the forcibly killed supervisor could write its own final sidecar. The bounded O observer intentionally **does not read stdin**, so stopping O kills its supervisor rather than sending an ineffective QUIT. If handoff cannot prove the previous process gone within the existing 3s bound, it fails closed. No silent second writer or rescue spawn.
- Original game native hosts now send best-effort committed-tick/generation breadcrumbs **after** real durable action success on Linux. Windows requires independent SQLite journal verification; no fake Windows breadcrumb counter.

## Windows crash-memory privacy and test strategy
The production app opts into SGDX Windows **second-chance debugger minidumps** only when `SGDX_WINDOWS_MINIDUMP=1` is explicitly set, since dumps may expose private process memory. Files are created under `<SQLITE>.diagnostics`, with private Windows NTFS ACLs. No dumps are uploaded to the user's chat, embedded in gameplay frames, or generated automatically by default. In the ephemeral CI test only, minidump opt-in is enabled for a real C++ access-violation fault fixture and the Godot/C++ integrated session; test verifies MDMP signature and ACLs, not merely that a file has a .dmp extension.

`SGDX03GodotInteractiveTests.py` launches the **actual compiled supervisor and children** through stdin/stdout pipes and tests: unmodified two-way frames, graceful QUIT, forced external termination of the child, forcibly killed guardian and no live child orphan, real child crash and process incident, bounded noninteractive watchdog, actual original finite-rock SQLite cockpit seat→undock→thrust→quit→restart, SQLite integrity and original ore persistence. Existing SGDX-02 process/SQLite/Windows minidump suites also run. `fp3d_env01_live_scene_acceptance.gd` imports the real Godot 4.7.2 scene and launches the *bundled supervisor*, not direct host, to exercise collision, camera, and original telemetry. Static source contracts are separate and **cannot** close OS runtime gates.

## Acceptance and remaining limitations
- Require exact-head successful MSVC x64 Windows and native Linux compile + CTest, real Python supervised subproc tests, genuine Godot runtime, real Windows MDMP validation and uploaded artifacts. Treat queued/cancelled workflows as **no evidence of success**.
- Windows precompiled ZIP must contain supervisor, station, cockpit and bounded observer executables, SQLite DLL, manifest, verified launch script and unchanged Godot project. Missing SGDX fails launch; do not fallback to direct C++.
- **Still unverified** until an actual Windows desktop session: physical GPU rendering, real camera position/occlusion, frame pacing, ship collision in interactive play, restart under abrupt power loss, and all descended-process edge cases. Linux process groups + parent-death signal do not by themselves prove containment of every escape-capable grandchild; do not overstate it as cgroup-v2 custody.
- Ship-module design remains deferred until authoritative 3D world/playable-space acceptance per creator's explicit decision.

## SGDX-03A — delayed guardian handoff fencing (2026-11-09)

Source review found that the original Godot station and cockpit sessions forgot a
stopped SGDX supervisor PID after a fixed three-second bookkeeping window. If an
old guardian was still alive (for example, hung during QUIT/SQLite shutdown),
pressing H/P later could try to open another writer without checking that live
PID. The SQLite single-writer lease could still reject an overlapping child, but
client-side admission must fail *before* launching any overlapping process.

- Both `native_player_session.gd` and `native_station_session.gd` now retain
  and check the last guardian PID for as long as `OS.is_process_running` reports
  it alive. Their same-role `start()` methods also reject a live previous PID.
  Dead PID records are cleared on observation rather than after an arbitrary
  time interval. A rare OS PID reuse fails closed rather than bypassing custody.
- A failed P↔H handoff no longer turns local unauthoritative walking back on
  while the prior writer is still alive. The operator receives a recovery/status
  message and can retry after that guardian exits; no second native simulation
  or SQLite writer is manufactured to repair the state.
- The source contract now asserts indefinite live-PID tracking, same-role
  overlap refusal, and fail-closed native handoff timeout.
- The real Godot scene test, with an actual native C++ cockpit already running
  under SGDX, constructs a second session whose prior-guardian timestamp is
  **ten seconds old** but whose PID is the still-live SGDX supervisor. It requires
  `process_id()` to retain that process and `start()` to refuse an overlapping
  host. If regression occurs, it kills the accidental second session and fails.

**Evidence status:** exact-head files and registered tests are committed;
actual Godot/Linux/Windows runtime execution is still **OPEN**. No clean exit,
Windows MDMP, GPU scene, or crash-recovery acceptance is claimed solely from
these source changes. The FP3D-ENV-01/SGDX workflow on the updated PR #125 head
must actually execute its native and Godot tests before this gate can close.

## 2026-11-09 independent native Linux group evidence — 18/18

An actual GitHub Actions **immutable source-export** run
[42673704458](https://github.com/vjmichael/Single-galaxy/actions/runs/42673704458)
produced artifact `SingleGalaxy-exact-head-source-1cdc2d4f` for source SHA
`1cdc2d4f5f6684e53aded6f220908a5263905166`; the downloaded tarball
passed its embedded SHA-256 checksum. The only subsequent implementation
change before this evidence was the SGDX03 test-only bounded-pipe-read repair,
Git blob `ffd288675a89b82f969809a494ba620ad1d3dc63`, independently
matched via `git hash-object` to the commit on PR #125
`eeb329a1f683ea01bca1871d36c6081c9e808973`.
No native source files changed between these two PR heads.

**Real Linux machine-run evidence (not simulated / not GH-hosted acceptance):**
Debian Linux x86_64, GCC 14.2, CMake 3.31.6, Ninja and SQLite 3.46.1.
`cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
-DCMAKE_CXX_FLAGS="-O0 -g0" -DBUILD_TESTING=ON
-DBUILD_BENCHMARKS=OFF` configured successfully. Actual kernel, station,
cockpit, observer, SGDX guardian and fault child, native cell worker, colony and
interactive hosts, all listed collision/boarding/ore/refinery test executables
compiled and linked. Python source contracts and py_compile also passed.
`ctest --test-dir build/native --no-tests=error --timeout 60
--output-on-failure -R <the exact main task-group Linux regex>`
completed as a **single local CTest process** with **18/18 PASS**, exit
code 0, 37.40 seconds real elapsed. These 18 include supervised crash,
child/guardian kill and SQLite recovery; original headless C++ ship–rock
contact and docking, station person walk/boarding, the new
`galaxy_play_station04c_native_unload_pipe`, original source-tagged
1 tonne unload -> 1000kg refinery, restart/integrity, full interactive
simulation hosts and SGDX03 native bridge. Test-only Godot source admission
passed but this machine did not execute the Godot engine.

A real reproducibility issue was observed: command-tool-attached foreground
invocations of this long CTest group sometimes stopped showing completion
at the SGDX03 native bridge, although the bridge succeeded when run separately
and wrote its terminal PASS output. In PR commit `eeb329a`, the test now
bounds **every** native-pipe response using a platform-independent daemon
reader thread and explicit 15-second deadline, closes the guardian on timeout
and reports the exact response phase; no false receipts or silent retries.
A detached **whole group** then finished cleanly with 18/18 PASS, proving
the full group can complete. This does **not** prove the foreground harness
symptom can never recur; the actual Windows CI runner still must execute.

**Not accepted by this Linux evidence:** real Windows/MSVC x64 compiler and
runtime, MDMP bytes/NTFS ACL in Windows, Godot 4.7.2 headless native scene,
non-headless Windows GPU/camera/collision, precompiled installer/ZIP artifact
and the full player-generated mining equipment loop. The original
[42669612497](https://github.com/vjmichael/Single-galaxy/actions/runs/42669612497)
was pinned to **older** source `3186678f...`, and its Windows/Linux
workers were queued rather than accepted. Request exactly **one** new gate
on the final current PR head after group closeout and repair any real failures
from its logs. PR #125 remains draft. No manufactured mining module is
unlocked from this Linux result.

### Independent optimized Linux Release rerun

The same native source compiled under CMake `-DCMAKE_BUILD_TYPE=Release`
(GCC 14.2 / SQLite 3.46.1) with **all** original-kernel, SGDX, station,
cockpit, observer, simulated live hosts and physical/ore/regression targets.
The complete **same 18-case CTest group** then completed as one local
process with **18/18 PASS**, exit code **0**, elapsed **36.40 seconds**.
Debug also independently passed 18/18 in 37.40 seconds. These are genuine
native Linux compiler and child-process results for this task group, not
source-only reviews, but do **not** certify Windows MSVC, Godot 4.7.2 engine,
an installed Windows archive, real GPU play or private Windows MDMP/ACL.
The exact latest PR head must still be admitted through the single central
GitHub Actions Windows/Linux task-group workflow; PR remains draft.

## 2026-11-09 actual GitHub Windows MSVC acceptance failure and targeted repair

The consolidated run
[42679508383](https://github.com/vjmichael/Single-galaxy/actions/runs/42679508383)
executed genuine `windows-2022` MSVC native compilation of the original
C++ kernel, native cockpit/station/observer hosts, SGDX supervisor, fault
child and source fixtures. **Compilation SUCCEEDED.** vcpkg SQLite x64
install also succeeded. The native CTest group then executed **19 actual
Windows tests**, with **15 passing** and **four failures**. The failed
tests were:

- `galaxy_server_dx_sqlite_recovery`
- `galaxy_fp3d03b_native_flight_pipe`
- `galaxy_play_station04c_native_unload_pipe`
- `galaxy_sgdx03_godot_native_bridge`

The real log for all four showed `PermissionError: [WinError 32]` while
`tempfile.TemporaryDirectory` attempted to unlink each test's SQLite
database. For SGDX02, the test had already printed a **successful** process
supervisor/rollback/retry receipt before the cleanup exception. Independent
test assertions (collision/custody/refinery etc.) passed in the other
cases until final cleanup. Do not misclassify the overall Windows group
as passed: CTest was **15/19 FAILURE**, and the workflow correctly
**skipped** Godot, package verification and windowed play.

**Demonstrated failure mechanism:** Python's SQLite connection context
manager `with sqlite3.connect(...) as con:` commits/rolls back its
transaction but **does not close** the connection on exit. Linux permits
unlink of an open database, masking that resource leak; Windows correctly
refuses deletion of the live handle. The four source tests have now been
repaired with `contextlib.closing(sqlite3.connect(...))`; write transactions
retain `with con:` to ensure commit/rollback before the explicit close.
The repair covers all eight SQLite test connection contexts in these four
test programs. **No native broker or Windows process security bypass was
added**, no ignore-errors deletion, fake passing receipt or Windows-only
test skip.

**Evidence status:** Windows MSVC compilation is genuinely verified on
the preceding exact PR head `4019e611...`; the four connection-closure
repairs and subsequent FP3D-WIN02 bundled-runtime source have **not yet**
executed in Windows CI. A single final task-group rerun is required; inspect
actual CTest Windows results, original Godot 4.7.2 on Windows/Linux,
minidumps/NTFS ACLs and the self-contained packaged ZIP before promotion.
This is separate from the owner's physical GPU playtest.
