# SPACE-05J — 1,024-tick original-world four-cell recovery and endurance

**Date:** November 8, 2026. **Branch:** `space/worker-io-deadline-20261107`, draft PR #95 (stacked on #93).
**Acceptance classification:** SOURCE IMPLEMENTED; native full-kernel Windows/Linux execution **OPEN**. Do not label 1,024-tick or Job Object acceptance as passed from a GitHub workflow that never executed a step.

## Why this task group

The existing `tests/NativeGalacticProbeSupervisorTests.cpp` could run 256 or 1,024 ticks via the `SPACE_ENDURANCE_TICKS` environment variable, but the original CMake project registered only the default **256-tick** `galaxy_native_galactic_supervisor` test. Neither normal CTest evidence nor the SPACE Windows/Linux matrix required a distinct 1,024-tick result. Its report also hardcoded eight supervisor instances, inaccurate for 1,024 ticks.

## Changes

- `tests/NativeGalacticProbeSupervisorTests.cpp`: preserve actual original-world ship/entity/NPC identifiers at initialization and compare recovered SQLite entity/NPC IDs at each 64-tick checkpoint, in addition to canonical world bytes and generation. Require a committed receipt naming a genuinely leased worker and cell; capture explicit per-cell completion counts, injected after-snapshot rollback/replay attempts, supervisor-object restarts, and independently opened recovered SQLite copies. Count actual restarts instead of emitting the old hardcoded `8` message. Preserve the one authoritative original SQLite writer; OS cell workers remain read-only supervised helpers.
- `CMakeLists.txt`: add `SPACE_ENABLE_1024_ENDURANCE` (default OFF). When enabled, register `galaxy_native_galactic_supervisor_1024` against the actual kernel-backed `galaxy_native_galactic_supervisor_tests` binary with `SPACE_ENDURANCE_TICKS=1024`, CTest timeout 900s and explicit endurance labels. The existing 256-tick test remains available and unchanged.
- `.github/workflows/space-native-galactic-supervisor.yml`: opt into the 1,024-tick CTest on Linux Ubuntu 24.04 and Windows Server 2022 with original CMake/SQLite/MSVC, checked-out exact PR SHA and real native worker executables. After the usual historical/native test matrix, run 1,024-tick separately with verbose output, separate JUnit and captured `space-1024-ctest.log`. Fail closed if test registration, CTest passing status, actual `1024` tick receipt or expected rollback/recovery cadence is missing.
- `scripts/space_linux_native_full_acceptance.sh`: the native Linux exact-head acceptance script also enables the option and executes 1,024-tick testing **separately** after the existing suite; it saves dedicated inventory, verbose logs and JUnit. Source/ref/clean tracked working tree requirements remain in force.
- IMPORTANT authoring repair: an intermediate version of the SPACE workflow incorrectly expanded shell lines due to JavaScript string replacement treating `$'` as a special substitution. GitHub rejected that version with no jobs. Recreated the entire workflow from its known-good original using literal-safe replacement; commit `c88426d56c31ef1c2d43b7af558c23787c30f3e5` restored GitHub's workflow recognition. Never use string replacement that interprets `$'` when editing literal GitHub YAML shell patterns.

## Acceptance invariants: genuine original-world ticks

| Check | 256 ticks | 1,024 ticks |
|---|---:|---:|
| Four separately leased physical cell workers | 4 | 4 |
| Successfully committed world ticks | 256 | 1,024 |
| Supervisor-object restarts every 32 ticks after initial construction | 7 | 31 |
| Intentional `AfterSnapshotWrite` failpoint + identical dispatch resume at tick indices 17, 54, 91, ... | 7 | 28 |
| Independent SQLite recovery every 64 ticks, compared with the live original world | 4 | 16 |
| Preserve original entity IDs, NPC IDs, ore quantity, world generations | Required | Required |
| Count and report actual per-cell completions | Required | Required |
| p50, p95, max end-to-end tick wall time from executed test | Measure | Measure |

**Do not confuse:** four supervised cell lanes with four independent writers; these tests retain the original single authoritative SQLite writer, not multi-writer sharding. The OS process-start microbenchmark is not equivalent to visible combat or movement frame latency.

## How to execute (full exact-head source checkout; no Docker)

```bash
# Option A — dedicated CMake native build on Linux
cmake -S . -B build/space-05j -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=OFF \
  -DSPACE_ENABLE_1024_ENDURANCE=ON
cmake --build build/space-05j --parallel 2 --target \
  galaxy_native_cell_worker galaxy_native_cell_hang_worker \
  galaxy_native_galactic_supervisor_tests
ctest --test-dir build/space-05j -N -R '^galaxy_native_galactic_supervisor_1024$'
ctest --test-dir build/space-05j -R '^galaxy_native_galactic_supervisor_1024$' \
  --verbose --timeout 900 --output-on-failure \
  --output-junit build/space-05j/space-1024-results.xml

# Option B — complete full-game native Linux acceptance at exact current SHA
scripts/space_linux_native_full_acceptance.sh "$(git rev-parse HEAD)"
```

On Windows, use the original SPACE matrix with Visual Studio 2022 and vcpkg SQLite SDK (the same HEAD) rather than treating the Linux-hosted Clang23 cross-build as Windows runtime acceptance. Retain JUnit, test count and actual server logs. If CI reports a zero-step failure, investigate runner provisioning, not the game compiler.

## Current evidence and remaining gates

- **SOURCE IMPLEMENTED:** end-to-end four-cell 1,024-tick C++ assertions, the opt-in CMake CTest registration, separate real-native Windows/Linux GitHub workflow stage, and independent local Linux acceptance path.
- **WORKFLOW PARSER FIXED:** after reverting the malformed intermediate workflow, GitHub recognized the original `SPACE-04C-04D-05B original galaxy / probes / multi-cell supervisor` workflow again and queued the new HEAD.
- **NO NEW NATIVE 1,024-TICK PASS:** the local container has a working isolated SGDX C++ component source and LLVM23 toolchain but does not have an exact-head full original `galaxy_kernel` checkout. The older 256-tick implementation and historical v66 144/144 full-kernel results are not evidence that this new 1,024-tick PR-head scenario passes. GitHub's recent original-world matrices have repeatedly failed at runner startup with zero steps; latest result must be inspected separately.
- **OPEN:** exact-head native Linux full kernel + 256 and 1,024 CTest runs; native Windows full kernel + 256 and 1,024; original SGDX-02 Windows Job Object/MDMP gate; sustained long-running installed-game p50/p95/max user-visible interaction performance.
- **NEXT PARALLEL GROUP AFTER THIS:** full cross-system physical interaction audit for rescue, passenger movement, ship destruction and custody; verify no lost/duplicated physical NPCs or cargo across recovery. Keep it separate from this test registration.

No independent executed test success is claimed by this checkpoint. Only close after native OS jobs genuinely execute and produce passing compiler/test artifacts.
