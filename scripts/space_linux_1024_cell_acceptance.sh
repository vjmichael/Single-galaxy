#!/usr/bin/env bash
# SPACE-05N: opt-in sustained native acceptance. Run only AFTER the full
# Linux exact-head acceptance succeeds on this same clean commit.
set -Eeuo pipefail
if [[ $# -ne 1 || ! "$1" =~ ^[0-9a-fA-F]{40}$ ]]; then
  echo "Usage: $0 <exact-40-character-commit-SHA>" >&2
  exit 64
fi
expected="${1,,}"
if [[ "$(uname -s)" != Linux ]]; then
  echo "Requires native Linux." >&2
  exit 69
fi
for tool in git ctest; do
  command -v "$tool" >/dev/null || { echo "Missing $tool" >&2; exit 69; }
done
root="$(git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)"
actual="$(git -C "$root" rev-parse HEAD)"
if [[ "$actual" != "$expected" || -n "$(git -C "$root" status --porcelain --untracked-files=no)" ]]; then
  echo "Refusing dirty or wrong exact-head source." >&2
  exit 65
fi
build_dir="${SPACE_BUILD_DIR:-$root/build/space-linux-${actual:0:12}}"
full_evidence="${SPACE_EVIDENCE_DIR:-$root/build/space-evidence-${actual:0:12}}"
status_file="$full_evidence/acceptance-status.txt"
if [[ ! -f "$status_file" || ! -f "$full_evidence/exact-head.sha" ||
      "$(cat "$full_evidence/exact-head.sha")" != "$actual" ]]; then
  echo "Missing full exact-head Linux acceptance evidence." >&2
  exit 65
fi
if ! grep -Fxq "exit_code=0" "$status_file" || ! grep -Fxq "sha=$actual" "$status_file" ||
   [[ ! -s "$full_evidence/ctest-results.xml" ]]; then
  echo "Full exact-head Linux CTest is not accepted." >&2
  exit 65
fi
if ! command -v python3 >/dev/null 2>&1 ||
   ! python3 "$root/scripts/space_classify_ctest.py" "$full_evidence/ctest-results.xml" >/dev/null; then
  echo "Full Linux JUnit contains failures/skips or cannot be validated." >&2
  exit 65
fi
if [[ ! -d "$build_dir" ]]; then
  echo "Exact-head CMake build directory unavailable." >&2
  exit 65
fi
endurance="$full_evidence/endurance-1024"
mkdir -p "$endurance"
printf '%s\n' "$actual" > "$endurance/exact-head.sha"
set +e
SPACE_ENDURANCE_TICKS=1024 ctest --test-dir "$build_dir" \
  -R '^galaxy_native_galactic_supervisor$' --output-on-failure \
  --timeout 600 --output-junit "$endurance/ctest-results.xml" \
  > "$endurance/ctest.log" 2>&1
status=$?
set -e
printf 'exit_code=%s\nsha=%s\nendurance_ticks=1024\n' "$status" "$actual" > "$endurance/status.txt"
if [[ "$status" -ne 0 ]]; then
  cat "$endurance/ctest.log" >&2
  exit "$status"
fi
python3 "$root/scripts/space_classify_ctest.py" "$endurance/ctest-results.xml" \
  --json-out "$endurance/classification.json" > "$endurance/classification.log"
cat "$endurance/ctest.log"
echo "PASS 1024-tick four-cell native CTest at $actual"
