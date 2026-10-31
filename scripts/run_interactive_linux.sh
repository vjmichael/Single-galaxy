#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
exe="${1:-.build/galaxy_interactive_host}"
if [ ! -x "$exe" ]; then
  printf '%s\n' 'Build the C++17 target galaxy_interactive_host with CMake first.' >&2
  exit 2
fi
printf '%s\n' 'Open http://127.0.0.1:8766/viewer/interactive.html (Ctrl+C stops host)' >&2
exec python3 live/interactive_relay.py --host-exe "$exe" --port 8766 --tick-ms 100 --max-ticks 600
