#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
exe="${1:-.build-live/galaxy_live_scenario_host}"
if [ ! -x "$exe" ]; then
  echo 'First build the native host with CMake (see README).' >&2
  exit 2
fi
exec python3 live/relay.py --host-exe "$exe" --port 8765 --tick-ms 100
