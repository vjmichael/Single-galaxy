# SGDX-02 supported Windows MSVC acceptance gate — 2026-11-08

Repository: `vjmichael/Single-galaxy`, draft PR #95, branch `space/worker-io-deadline-20261107`.

## Required order (owner decision)

1. Build **real** `src/server_dx_supervisor.cpp` and `tests/ServerDx02FaultChild.cpp` using a **supported native Windows MSVC compiler** (Visual Studio 2022 x64, not a Clang 17 STL compatibility override).
2. On Windows, run the **real** `sgdx02_supervisor` CTest: second-chance access violation produces private `MDMP`, timeout kills the descendant through the Job Object, original stream forwarding and JSON semantics, protected NTFS DACL for incidents/dumps, no private dump uploaded automatically. Run `sgdx02_sqlite_recovery` as a separate Windows test.
3. Only if both tests succeed, start exact-head **original-game host/worker recovery** on Windows and Linux. Game authority and SQLite single writer remain unchanged.

## Changes committed in this pass

- `tests/sgdx02/CMakeLists.txt` accepts `-DSGDX_REQUIRE_NATIVE_MSVC=ON`; fails CMake configuration unless Windows, x64 and `CMAKE_CXX_COMPILER_ID=MSVC`. This is a **fail-closed acceptance gate** distinct from the prior proven-but-unsupported Linux-hosted Clang 17 cross-build.
- `tests/SGDX02SupervisorTests.py` repaired contradictory Windows segmentation-fault checks (a second-chance exception cannot simultaneously be classified `nonzero_exit`). The Windows stderr contract forwards the original stream unchanged; unlike Linux, stderr is not reported in `stderr_tail`. The test explicitly checks this distinction; invalid UTF-8 output is decoded with replacement for the stream assertion. Descendant timeout raised to 2.5 seconds for genuine Windows startup.
- Same Python test checks **protected NTFS DACLs** using `GetNamedSecurityInfoW` and `ConvertSecurityDescriptorToStringSecurityDescriptorW`: requires protected DACL, SYSTEM permission and no broad Everyone/Users/Authenticated Users access on real JSON incident and MDMP file.
- `.github/workflows/sgdx02-original-game.yml` now contains `windows-diagnostics-acceptance`, verifying the MSVC compiler identity, compiling actual Windows supervisor/fault executable, executing both native Windows CTests, retaining compiler/JUnit evidence **without uploading crash dumps**. Existing `original-game-sgdx` Windows/Linux matrix now `needs: windows-diagnostics-acceptance` and **cannot run when Windows DX fails**.
- Existing standalone Linux/Windows matrix remains as a separate independent signal.

## Executed results

- **Local native Linux GCC**: independent component SGDX supervisor and SQLite recovery, `2/2 CTest PASS`, 2026-11-08.
- **Local native Linux Clang 17**: same suite, `2/2 CTest PASS`, 2026-11-08.
- **Local fail-closed CMake proof**: exact committed CMake source blob `6942f43864c5c86d62032812f92883a0ee707e84` rejects Linux GCC with `SGDX_REQUIRE_NATIVE_MSVC=ON`; also rejects Windows-target Clang 17 when the same flag is set. This proves the test cannot accidentally accept the unsupported prior cross-build.
- **Native MSVC Windows**: NOT executed. GitHub Actions `SGDX-02 original game hosts and worker containment`, run `42491976135`, Windows diagnostics job `128766113272` ended in `failure` with **zero executed steps**; the gated original-game job `128766625144` was `skipped`. Separate `SGDX-02 native supervisor fault matrix` run `42491976320` also ended in failure with zero steps on both OSes. These are **runner/job startup failures**, not tests or compiler errors. The precise runner refusal cause is not recoverable from the available job-steps view.
- Local container has Windows SDK 10.0.26100 and MSVC 14.44 headers/libs for cross-build, **not a native Windows OS**. Clang 17 is insufficient for supported MSVC 14.44 STL and must not be used to close this gate. No Wine/Windows VM or installed Clang 19+ is available locally. Debian package server DNS lookup also failed.

## Next execution

Investigate/restore actual Windows runner availability, then run the already committed Windows MSVC gate and read **real steps and logs**. Fix genuine MSVC compiler failures or runtime faults from that evidence and rerun exact head. Once `windows-diagnostics-acceptance` passes, the dependent original-world matrix may execute. If the OS runner still completes without any steps, **do not rerun repeatedly without a changed hypothesis**. Never claim the MDMP or Job Object gates closed from cross-compiled PE imports or source-only tests.

The private Windows cross-build cache and a restore README are in the user's Library: `/Jan Mod - Lu/Delivered/Windows Build Cache/`. Do not commit copyrighted MSVC/SDK binary payloads to the public repo.
