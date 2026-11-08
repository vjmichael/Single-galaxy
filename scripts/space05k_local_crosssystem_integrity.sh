#!/usr/bin/env bash
# SPACE-05K: exact-source native Linux physical interaction integrity acceptance.
# Requires the full checked-out original game kernel; no Docker or source substitute.
set -Eeuo pipefail
if [[ $# -ne 1 || ! "$1" =~ ^[0-9a-fA-F]{40}$ ]]; then
  echo "usage: $0 <40-character exact Git SHA>" >&2
  exit 64
fi
expected="${1,,}"
test "$(uname -s)" = Linux || { echo "Native Linux required" >&2; exit 69; }
for x in git cmake ninja c++ ctest; do
  command -v "$x" >/dev/null || { echo "Missing $x" >&2; exit 69; }
done
root="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"
actual="$(git -C "$root" rev-parse HEAD)"
[[ "$actual" == "$expected" ]] || { echo "Wrong SHA $actual expected $expected" >&2; exit 65; }
[[ -z "$(git -C "$root" status --porcelain --untracked-files=no)" ]] ||
  { echo "Tracked source dirty; refuse exact-head acceptance" >&2; exit 65; }
build="$root/build/space05k-${actual:0:12}"
evidence="$root/build/space05k-evidence-${actual:0:12}"
mkdir -p "$evidence"
printf '%s\n' "$actual" > "$evidence/source.sha"
{ date -u; c++ --version | head -1; cmake --version | head -1; } > "$evidence/native-environment.txt"
on_exit(){
  ec=$?
  printf 'exit_code=%s\nsource_sha=%s\n' "$ec" "$actual" > "$evidence/status.txt"
  echo "SPACE-05K evidence in $evidence" >&2
}
trap on_exit EXIT
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DBUILD_BENCHMARKS=OFF \
  -DSPACE_ENABLE_1024_ENDURANCE=OFF 2>&1 | tee "$evidence/configure.log"
cmake --build "$build" --parallel "${SPACE05K_BUILD_JOBS:-2}" --target \
  galaxy_native_cell_worker galaxy_native_cell_hang_worker \
  galaxy_native_galactic_supervisor_tests \
  galaxy_flight05_physical_rescue_tests \
  galaxy_flight06_dependent_rendezvous_tests \
  galaxy_play09_wreck_salvage_tests \
  v59_cross_system v59_dependent_custody v59_ferry_loss \
  v59_ferry_loss_integrity v59_mixed_ems v59_mixed_integrity \
  v59_physical_handoff v59_crash_custody v59_third_party_custody \
  2>&1 | tee "$evidence/build.log"
pattern='^(galaxy_native_galactic_supervisor|galaxy_flight05_physical_rescue|galaxy_flight06_dependent_rendezvous|galaxy_play09_wreck_salvage|v59_cross_system|v59_dependent_custody|v59_ferry_loss|v59_ferry_loss_integrity|v59_mixed_ems|v59_mixed_integrity|v59_physical_handoff|v59_crash_custody|v59_third_party_custody)$'
ctest --test-dir "$build" -N -R "$pattern" 2>&1 | tee "$evidence/inventory.log"
grep -q 'Total Tests: 13' "$evidence/inventory.log" ||
  { echo "Missing one or more original-world tests" >&2; exit 65; }
ctest --test-dir "$build" -R "$pattern" --output-on-failure \
  --timeout 300 --output-junit "$evidence/native-results.xml" \
  2>&1 | tee "$evidence/ctest.log"
grep -q '100% tests passed, 0 tests failed out of 13' "$evidence/ctest.log" ||
  { echo "Actual original-world custody tests were not all passing" >&2; exit 65; }
echo "PASS SPACE-05K native exact-head original-world 13/13, including new physical destruction regression"
