#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
exe="${1:-build/galaxy_colony_live_host}"
if [ ! -x "$exe" ]; then
 echo 'Build galaxy_colony_live_host with CMake before launching' >&2;exit 2
fi
save="${2:-${HOME}/.local/share/single-galaxy/colony.sqlite}"
mkdir -p "$(dirname "$save")"
printf '%s\n' 'Colony-first test world: http://127.0.0.1:8766/viewer/interactive.html' >&2
exec python3 live/interactive_relay.py --host-exe "$exe" --colony --save "$save" --port 8766 --tick-ms 100 --max-ticks 1200
