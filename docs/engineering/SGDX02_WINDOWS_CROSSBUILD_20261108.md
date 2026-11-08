# SGDX-02 Windows x64 cross-build acceptance (Linux-hosted)

**Date:** 2026-11-08 UTC. **Repository:** `vjmichael/Single-galaxy`, PR #95, `space/worker-io-deadline-20261107`.

**Scope:** Native Windows **cross-compilation and PE import-link verification only**. This is not a Windows runtime, minidump capture, Job Object, original installed-game or full-world acceptance claim.

## Input provenance and reproducibility

The user previously supplied a persistent, private Windows toolchain in ChatGPT Library:
`/Jan Mod - Lu/Delivered/Windows Build Cache/Win64_MSVC_WindowsSDK_CMake_Ready_v2.zip`. SHA-256 `8b0ff2e0a5c0dedce362963f97b63c5525b76a4b3eeb6c10993bfcc748412bdd`.

- MSVC 14.44.35207 C++ headers, x64 CRT, Windows SDK 10.0.26100 selected x64 headers and import libs, `DbgHelp.Lib`, `AdvAPI32.Lib`.
- Host: Linux CMake 3.31.6, Ninja, Clang 17 `clang-cl` and `lld-link`.
- `include/galaxy/ServerWindowsMinidumpApi.hpp` prefers the canonical Microsoft `DbgHelp.h` when `minidumpapiset.h` is present. The preserved SDK cache lacks this latter header, so the header provides an **explicit narrow ABI declaration** for only `MiniDumpWriteDump`, `MINIDUMP_EXCEPTION_INFORMATION`, `MINIDUMP_TYPE` and the two opaque optional structs. It uses the *real* SDK `DbgHelp.Lib`, not a fake header claiming to implement Microsoft APIs. `static_assert` checks Win64 exception structure layout. Compare to Microsoft Learn official API type/function docs before changing the ABI.
- CMake `tests/sgdx02/CMakeLists.txt` now links `dbghelp` and `advapi32` in the case-sensitive Linux-host SDK layout and **does not inject Linux `pthreads` into the Windows fault test executable**.
- The source provides `tests/sgdx02/WindowsClangToolchain.cmake` so the same toolchain cache can be restored without rebuilding the original multi-GB MSVC/SDK source archives.

## Local exact build command

Restore the v2 cache to `/mnt/data/windows-build-toolchain` if missing, verify the SHA-256, then run `bash /mnt/data/windows-build-toolchain/portable/bootstrap-full-sdk.sh` to re-create SDK case aliases.

```bash
export SG_WINDOWS_TOOLROOT=/mnt/data/windows-build-toolchain
cmake -S tests/sgdx02 -B build/sgdx02-windows-x64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/tests/sgdx02/WindowsClangToolchain.cmake" \
  -DSGDX_ALLOW_UNSUPPORTED_CLANG17=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/sgdx02-windows-x64 --parallel 2 \
  --target galaxy_server_dx_supervisor galaxy_server_dx_fault_child
file build/sgdx02-windows-x64/*.exe
objdump -p build/sgdx02-windows-x64/galaxy_server_dx_supervisor.exe
```

The `SGDX_ALLOW_UNSUPPORTED_CLANG17` flag explicitly overrides **MSVC 14.44's STL compiler minimum of Clang 19**. This override was necessary because the available host compiler is Clang 17. It is an **unsupported cross-build compatibility mode** and must not be equated with a supported compiler/CRT runtime combination. The Windows application must be rebuilt with supported MSVC or Clang 19+ and tested on Windows before production acceptance.

## Actual native cross-build evidence

On the full source versions used for this pass (source tree anchored at commit `f96ee39f16d11257a6198f31a3fd133456d89d13`):

- `galaxy_server_dx_supervisor.exe`: **PE32+ Windows x86-64**, 137,728 bytes; SHA-256 `00e8d99c3e961df6bc6dd496070aa5cb3a806997acfddc95b52fe2ffab35a27e`.
- `galaxy_server_dx_fault_child.exe`: **PE32+ Windows x86-64**, 108,032 bytes; SHA-256 `a143ad1d27cedb8fb9ce1b7bd50a90a6903a1629d67467920e05cf26c1565357`.
- PE import table confirms `MiniDumpWriteDump` imported from `dbghelp.dll`, `SetNamedSecurityInfoW` from `ADVAPI32.dll`, and `CreateJobObjectW` / `CreateProcessW` from `KERNEL32.dll`.
- Both binaries compiled and linked through a clean CMake/Ninja cross-build with project source. This is **not** the unrelated 1 KiB linker smoke executable from the earlier toolchain setup.
- Linux component regression (same SGDX host/supervisor source, x64 Linux builds): GCC Release **2/2 CTest PASS**, Clang Release **2/2 CTest PASS after a bounded supervisor rerun**. The first combined Clang run stalled, so the separate bounded rerun is the evidence; keep investigation open if this recurs.

## Required next gates (OPEN)

1. **Native Windows compilation with supported compiler**, then Windows execution of `tests/SGDX02SupervisorTests.py`: actual second-chance `MDMP` signature and private ACL checks, Job Object descendant teardown, abort/timeout/cancellation, stderr isolation, rotation.
2. Test real Windows process exit codes, crash-time context quality and the `dbghelp.dll` version in the execution environment; do not assume imports guarantee crash dumps work.
3. Original game host/worker CMake acceptance and native SQLite journal recovery, not just the fault-child fixture. Original world remains single SQLite writer; DX is an observer.
4. Preserve the standalone and full-project CI matrix evidence only for **jobs that actually executed steps**. Historical no-runner failures are not compiler failures.
5. Prefer shipping an authentic matching official `minidumpapiset.h` in a subsequent curated cache revision when available and legally appropriate. The documented narrow ABI fallback is source code, not a substitute for the rest of the Windows SDK.

**Privacy/licensing:** Do not publish the user-owned MSVC/Windows SDK binaries into the public GitHub repo. Build evidence may include generated PE executables and source/logs, but SDK redistribution and executable runtimes need separate legal review.
