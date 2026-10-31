# B5.2 — Live Station Lifecycle Client: Engineering Report

**Date:** 2026-10-30  
**Scope:** Original single-galaxy local prototype, no EVE/EVEmu implementation or assets.  
**Acceptance:** Linux C++/SQLite end-to-end, Python relay integration, Node.js contract/HUD smoke. **Actual graphical-browser navigation was not verified** because available Chromium responded `ERR_BLOCKED_BY_ADMINISTRATOR`; Windows-native execution also remains unverified.

## Changes shipped

1. The local interactive host now **begins docked at Station 1**. Owner 7's Ship 2 has hold capacity 20 and a private hangar with capacity 100. Asteroid 3 has 40 ore, located within the initial undock mining range.
2. The existing owner-bound command path now supports `UNDOCK`, `DOCK` and `UNLOAD|<bounded-positive-amount>` in addition to thrust, start mining and stop mining. Clients **cannot choose their player ID, ship ID, target ID, amount for full unload, execution tick or command ID**. The relay's same-origin, anti-CSRF token and strict schemas remain.
3. The authoritative simulation already supports delayed docking/undocking and capacity-checked unloading; B5.2 does **not** replace that model. Requests remain asynchronous: HTTP `202 queued_not_committed` is not success until a committed `C` result and ensuing states are observed.
4. The SGVIEW v1 projection adds optional authenticated-by-projection-hash telemetry records: `Q|ship|dock_state|station|remaining_ticks`, `Z|ship` removal, `H|station|owner|ore|capacity`, and `V|station|owner` removal. These are hashed and validated in C++ and browser JS with no changed hashes for old B3 recordings that have no station state. **Compatibility is one-way:** the updated decoder reads older recordings, while unmodified B3 decoders will reject new Q/H records. These hashes detect transport divergence **but are not cryptographic authentication**.
5. The live HTML provides Undock, Request dock, and Unload ore controls plus docking state/countdown, local hangar contents, and committed command/event receipts. The browser's visuals remain 2D schematic Canvas; it is **not** a finished game client.
6. A committed host-generated `.sgview` trace is included as a stationary regression fixture with the actual 20-unit ore transfer.

## Verified results

| Gate | Result |
|---|---|
| Native host command lifecycle | PASS: docked -> undock -> mine 20 -> dock -> unload 20 |
| Local hangar and hold conservation | PASS: hangar 0 -> 20, ship cargo 20 -> 0; source asteroid 40 -> 20 |
| Two independent SSE observers | PASS: same initial snapshot + all deltas |
| Late reconnection | PASS: identical replay; no second world created |
| HTTP command schema | PASS: cross-origin, invalid token, client-supplied actor/amount rejected |
| In-transition denial | PASS: premature dock returns authoritative `IN_TRANSITION` result |
| SGVIEW backward compatibility | PASS: legacy B3 Node tests still pass |
| Projection tamper checks | PASS: altered dock/hangar telemetry fails validation or projection hash |
| Live HUD in mocked Node DOM | PASS: initial docked/empty and terminal docked/20 hangar displayed |
| C++ CTest suites | PASS: 6/6 (including B5.2 end-to-end) |
| Node.js tests | PASS: 26/26 |
| Actual Chromium page rendering | **UNVERIFIED:** local Chromium admin policy blocks navigation |
| Windows x86-64 runtime/install | **UNVERIFIED:** Linux build only; no Windows installer produced |

Note: This is a new B5.2 acceptance cycle, not proof that every full lifecycle variation (including manual thrust return from distant positions) has been visually exercised. The headless B5 station tests remain separately covered.

## Run on Linux (developer environment)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html` on the *same machine*. Click **Undock**, then **Mine 4/tick**, wait until the hold is 20/20, click **Request dock**, and then **Unload ore**. The hangar should display `20 / 100` and cargo `0 / 20`. If the ship is moving or away from the station, the authoritative simulation rejects docking; the demo does not have a movement braking assist.

## Limitations / non-goals

- Loopback-only developer relay with a shared single-owner session; its anti-CSRF token is not full player authentication or privacy isolation. Hangar details should be owner-filtered before any real multiplayer implementation.
- Simulation runs for a bounded tick count and creates a fresh temporary SQLite database for each relay launch. The persisted world exists throughout that run and is committed before frames are published; the relay deletes its temporary save on exit. It is not yet a persistent save-slot UI.
- No ship cargo loading or Station B market/trade yet; those belong to subsequent logistics integration.
- Existing `tests/legacy/B43InteractiveEndToEndTests.py` belongs to the old initially-undocked B4.3 host and is preserved for historical comparison. The current `tests/InteractiveEndToEndTests.py` routes to the B5.2 test.
- Direct flight, 3D visuals, refineries, scanning, NPC workforce and material recycling are brainstorming/roadmap features, not B5.2 code.

## Next integration gate

Implement ship loading from an owner/station-specific hangar, station-to-station transportation within the current simulated world (and later between systems), and an atomic market credit/ore ledger. Verify transfer replay, capacity denial, wrong-station denial, double-spend/duplicate prevention, and persistence/restart recovery.
