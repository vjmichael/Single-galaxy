#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="${1:-$root/build}"
tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
"$bin/galaxy_client_wire_producer" "$tmpdir/world.sqlite" | "$bin/galaxy_client_wire_consumer" | tee "$tmpdir/output.txt"
grep -q 'client tick=3 seq=3 cargo=10/10 ore=10 event=MINING_STOPPED:CARGO_FULL' "$tmpdir/output.txt"
grep -q 'client tick=5 seq=5 cargo=10/10 ore=10' "$tmpdir/output.txt"
echo 'B3 two-process smoke PASS'
