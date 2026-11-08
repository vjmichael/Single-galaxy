#!/usr/bin/env bash
# SPACE native Linux acceptance. Only the exact checked-out original source
# and real CMake/CTest results are admissible evidence. No Docker.
set -Eeuo pipefail
if [[ $# -ne 1 || ! "$1" =~ ^[0-9a-fA-F]{40}$ ]]; then
  printf 'Usage: %s <exact-40-character-Git-head-SHA>\n' "$0" >&2
  exit 64
fi
expected="${1,,}"
if [[ "$(uname -s)" != Linux ]]; then
  printf 'This acceptance script requires native Linux.\n' >&2
  exit 69
fi
for tool in git cmake ninja c++ ctest; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    printf 'Missing native tool: %s\n' "$tool" >&2
    exit 69
  fi
done
root="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
actual="$(git -C "$root" rev-parse HEAD)"
if [[ "$expected" != "$actual" ]]; then
  printf 'Wrong source: expected=%s actual=%s\n' "$expected" "$actual" >&2
  exit 65
fi
if [[ -n "$(git -C "$root" status --porcelain --untracked-files=no)" ]]; then
  printf 'Tracked working tree is modified; cannot claim exact-head acceptance.\n' >&2
  exit 65
fi
build_dir="${SPACE_BUILD_DIR:-$root/build/space-linux-${actual:0:12}}"
evidence_dir="${SPACE_EVIDENCE_DIR:-$root/build/space-evidence-${actual:0:12}}"
mkdir -p "$evidence_dir"
printf '%s\n' "$actual" > "$evidence_dir/exact-head.sha"
{
  printf 'date_utc='; date -u +'%Y-%m-%dT%H:%M:%SZ'
  printf 'host='; uname -a
  printf 'compiler='; c++ --version | head -1
  printf 'cmake='; cmake --version | head -1
  printf 'ninja='; ninja --version
} > "$evidence_dir/environment.txt"
on_exit(){
  local exit_code=$?
  printf 'exit_code=%s\nsha=%s\n' "$exit_code" "$actual" > "$evidence_dir/acceptance-status.txt"
  # Classify any real CTest JUnit even when CTest fails under pipefail.
  # A failed classifier must never turn a failing native build green.
  if [[ -s "$evidence_dir/ctest-results.xml" ]] && command -v python3 >/dev/null 2>&1; then
    python3 "$root/scripts/space_classify_ctest.py"       "$evidence_dir/ctest-results.xml"       --json-out "$evidence_dir/ctest-failure-classification.json"       > "$evidence_dir/ctest-failure-classification.log" 2>&1 || true
  fi
  printf 'Native evidence: %s\n' "$evidence_dir" >&2
}
trap on_exit EXIT
jobs="${SPACE_BUILD_JOBS:-2}"
test_jobs="${SPACE_TEST_JOBS:-2}"
for count in "$jobs" "$test_jobs"; do
  if [[ ! "$count" =~ ^[1-9][0-9]*$ ]]; then
    printf 'Invalid build/test parallelism: %s\n' "$count" >&2
    exit 64
  fi
done
cmake -S "$root" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=OFF \
  2>&1 | tee "$evidence_dir/configure.log"
cmake --build "$build_dir" --parallel "$jobs" --target galaxy_kernel \
  2>&1 | tee "$evidence_dir/kernel-build.log"
cmake --build "$build_dir" --parallel "$jobs" \
  2>&1 | tee "$evidence_dir/all-targets-build.log"
ctest --test-dir "$build_dir" -N \
  2>&1 | tee "$evidence_dir/test-inventory.log"
ctest --test-dir "$build_dir" --parallel "$test_jobs" \
  --output-on-failure --timeout 180 --output-junit "$evidence_dir/ctest-results.xml" \
  2>&1 | tee "$evidence_dir/ctest.log"
cp -f "$build_dir/Testing/Temporary/LastTest.log" "$evidence_dir/LastTest.log" || true
printf 'PASS full native Linux kernel and registered CTest suite at %s\n' "$actual"
