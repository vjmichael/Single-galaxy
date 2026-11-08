# SGDX-02 — LLVM 23.1.5 Windows x64 cross-build acceptance checkpoint
**Date:** 2026-11-08 UTC. **PR:** #95, branch `space/worker-io-deadline-20261107`.

## Purpose and boundaries

The user supplied the official LLVM 23.1.5 **Linux x64** archive in three private Library parts. It resolves the MSVC 14.44 STL / Clang 17 incompatibility previously bypassed through an unsupported compiler override. This closes the **modern Clang-version Windows cross-compile/link gate**, not Windows-native MSVC, Job Object, crash-time minidump, or installed game acceptance.

### Private inputs (do not commit binaries to public repository)

- `Library /Jan Mod - Lu/Delivered/LLVM-23.1.5-Linux-X64.tar.zip.001`, `.002`, `.003`; concatenated ZIP SHA-256 `1d9d0f74a70ab8fd6630770022706dd2dbc25e08ecca0ce751d95a9dabf24ea4`. ZIP CRC checked; single member `LLVM-23.1.5-Linux-X64.tar.zst`. Note: Zstd frame uses a 1 GiB window, so ordinary `tar --zstd` failed. The successful method was `zstd -d --long=30 -c | tar -xf -` while streaming the inner member from Python `zipfile`. Extracted LLVM footprint approximately 12 GiB; allow sufficient scratch space.
- `Library /Jan Mod - Lu/Delivered/Windows Build Cache/Win64_MSVC_WindowsSDK_CMake_Ready_v2.zip` SHA-256 `8b0ff2e0a5c0dedce362963f97b63c5525b76a4b3eeb6c10993bfcc748412bdd`, MSVC 14.44 and SDK 10.0.26100. Run `portable/bootstrap-full-sdk.sh` after extraction to construct case-sensitive header/import aliases.

### Reproducible source and build

- `tools/sgdx02/build_windows_clang23_linux.sh` builds the **real** `src/server_dx_supervisor.cpp` and `tests/ServerDx02FaultChild.cpp` with CMake+Ninja+LLVM23's Linux `clang-cl`/`lld-link`, the actual Microsoft SDK headers and `dbghelp.lib`. Set `SG_WINDOWS_TOOLROOT` to the extracted v2 cache and `SG_LLVM23_ROOT` to the extracted `LLVM-23.1.5-Linux-X64` root.
- Used exact GitHub source blob hashes `074e363746b2ed622cfa3a998f69de723fa0d9c5` supervisor, `23618b206ce638908b729af4b40dd6da7a9f1045` fault-child, `6942f43864c5c86d62032812f92883a0ee707e84` CMake, `1777549806f384c4f0e453cfc700ced149b856c0` minidump ABI fallback.
- CMake explicitly used `SGDX_ALLOW_UNSUPPORTED_CLANG17=OFF` and `SGDX_REQUIRE_NATIVE_MSVC=OFF`. The former forbids the previously needed unsupported version override. The latter is deliberate: **this is cross-build evidence, not a supported native Windows MSVC gate**, which remains separately fail-closed with `SGDX_REQUIRE_NATIVE_MSVC=ON`.
- Executed a clean CMake rebuild of both sources with Clang version `23.1.5`. Both produced PE32+ Windows AMD64 binaries:
  - `galaxy_server_dx_supervisor.exe`: SHA-256 `de52b0ab88613bd840c1a828c6a7e7851e67fcfa6148a636a72d9ad98f8eacb1`.
  - `galaxy_server_dx_fault_child.exe`: SHA-256 `a7f5fe4a4b01bda9b2b0266433aceb1ae740a6fb6b1d9885140acd1e6e7d8175`.
- Supervisor PE import table references real Microsoft `dbghelp.dll!MiniDumpWriteDump`, `ADVAPI32.dll` and Windows Job Object interfaces. No compatibility override appears in `build.ninja`.
- Native Linux Clang **23.1.5** component CTest: `sgdx02_supervisor` PASS (10.47s), `sgdx02_sqlite_recovery` PASS (6.31s), **2/2 PASS**, on the same unchanged C++ source, via previous archived SGDX Python test fixtures (the newest Windows-only assertions were updated separately in the public repo).

## Remaining strict gates

- A real native MSVC compiler and Windows OS must **execute** the `windows-diagnostics-acceptance` job: actual second-chance MDMP capture, DACL privacy, Job Object descendant kill, and Windows SQLite recovery. Clang23 Linux-hosted crossbuild does not prove any of these runtime properties.
- Latest inspected GitHub workflows `42492512199` and `42492512327` failed without executed steps; gated original-game recovery was skipped. Check the runner rather than declaring a build failure from absent logs. No Windows OS, Wine or KVM found in the local execution container.
- Only after Windows diagnostics acceptance passes, execute the actual original-game host/worker crash recovery and four-cell persistence work per the user's strict order.
- Save compact logs, source hashes, JUnit and two generated EXEs as evidence; keep original LLVM and proprietary SDK archives solely in the user's private Library. Do not duplicate the 1.2 GiB LLVM ZIP for every handoff.
