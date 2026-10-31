#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bin="${1:-.build-b4}"
dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
for name in ten hold motion; do
  "$bin/galaxy_visual_scenario_recorder" "$name" "$dir/$name.sqlite" > "$dir/$name.sgview"
  "$bin/galaxy_client_wire_consumer" < "$dir/$name.sgview" > "$dir/$name.stdout"
  case "$name" in
    ten) grep -q 'cargo=100/100' "$dir/$name.stdout" ;;
    hold) grep -q 'event=MINING_STOPPED:CARGO_FULL' "$dir/$name.stdout" ;;
    motion) grep -q 'client tick=12' "$dir/$name.stdout" ;;
  esac
  echo "PASS scenario $name (real SQLite-backed producer → B3 consumer)"
done
