#!/usr/bin/env bash
# SGDX-02: genuine Windows x64 PE cross-build from Linux; no runtime acceptance.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
test -d "$SG_WINDOWS_TOOLROOT" || { echo 'Restore the private MSVC/SDK v2 cache first'; exit 2; }
test -x "$SG_LLVM23_ROOT/bin/clang-cl" || { echo 'Restore the private LLVM 23.1.5 Linux x64 archive first'; exit 2; }
export PATH="$SG_LLVM23_ROOT/bin:$PATH"
clang-cl --version | head -1 | grep -F 'clang version 23.1.5'
python3 "$SG_WINDOWS_TOOLROOT/portable/tools/generate_case_aliases.py" "$SG_WINDOWS_TOOLROOT/sdk/ready"
if test -f "$SG_WINDOWS_TOOLROOT/sdk/ready/Debuggers/inc/dbghelp.h" && ! test -e "$SG_WINDOWS_TOOLROOT/sdk/ready/Debuggers/inc/DbgHelp.h"; then
  ln -s dbghelp.h "$SG_WINDOWS_TOOLROOT/sdk/ready/Debuggers/inc/DbgHelp.h"
fi
python3 "$SG_WINDOWS_TOOLROOT/portable/tools/generate_include_aliases.py" \
  "$SG_WINDOWS_TOOLROOT/sdk/ready/Include/10.0.26100.0/um" \
  "$SG_WINDOWS_TOOLROOT/sdk/ready/Include/10.0.26100.0/shared" \
  "$SG_WINDOWS_TOOLROOT/sdk/ready/Include/10.0.26100.0/ucrt" \
  "$SG_WINDOWS_TOOLROOT/msvc/Contents/VC/Tools/MSVC/14.44.35207/include"
BUILD="$ROOT/build/sgdx02-clang23-windows"
cmake -S "$ROOT/tests/sgdx02" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/tests/sgdx02/WindowsClangToolchain.cmake" \
  -DSGDX_ALLOW_UNSUPPORTED_CLANG17=OFF -DSGDX_REQUIRE_NATIVE_MSVC=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel 2 --target galaxy_server_dx_supervisor galaxy_server_dx_fault_child
for exe in "$BUILD"/*.exe; do file "$exe" | grep -q 'PE32+ executable.*x86-64'; done
! grep -q '_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH' "$BUILD/build.ninja"
llvm-objdump -p "$BUILD/galaxy_server_dx_supervisor.exe" | grep -q 'MiniDumpWriteDump'
sha256sum "$BUILD"/*.exe
echo 'PASS actual SGDX Windows PE x64 Clang23 cross-build. Native Windows tests still open.'
