# Single Galaxy — B4 Live Simulation and Client Relay
**Date:** October 30, 2026  
**Scope:** B4.1 scheduler, B4.2 live browser data path, partial B4.4 consistency tests  
**Branch/repo:** none modified; standalone local source package. This is independent of EVEmu.

## Architecture

```text
SingleGalaxy Simulation + PersistentWorld SQLite
       | fixed-step clock (steady_clock / sleep_until)
       | SQLite COMMIT first; only afterward create SGVIEW v1 delta
       v
B4 galaxy_live_scenario_host (one process, stdout SGVIEW)
       v
Python 3 development relay (127.0.0.1 ONLY; SSE event data=base64(frame))
       v
Browser live.html + live.js + existing B3 viewer/contract
       | validates snapshot, contiguous sequence/tick, state hash
       v
Canvas view / cargo HUD / mining notification
```

**This is a temporary development transport, not production multiplayer networking.** No external game content or proprietary assets are used.

## Verified tests (actual execution, Linux)

- C++ GCC release build of `galaxy_live_scenario_host`: **PASS**.
- Scenario ten: **11 streamed frames** (snapshot + 10 ticks), 10 asteroids removed, ship holds 100 ore, total remains 100: **PASS**.
- Scenario hold: **8 streamed frames** (snapshot + 7 ticks), cargo reaches 10/10, asteroid retains 10 ore, `MINING_STOPPED:CARGO_FULL`: **PASS**.
- Scenario motion: **13 streamed frames** (snapshot + 12 ticks), deterministic movement: **PASS**.
- Independent existing C++ `galaxy_client_wire_consumer` accepted all three complete streams: **PASS**.
- Four prior CTest suites (simulation, persistence/recovery, mining lifecycle, client contract): **4/4 PASS** under GCC and Clang undefined-behavior sanitizer.
- Existing Node recorded-protocol/UI tests: **9/9 PASS**.
- New Node live protocol/UI tests: **8/8 PASS**, including missing-frame rejection, tampered-state refusal, cargo-full HUD, all rocks disappearing.
- Python integration: local HTTP health, static live HTML response, SSE transmission of eight frames, cargo-full event, completion event, and invalid scenario rejection: **PASS**.
- Clang UBSan build and one hold-live-host execution: **PASS**.

### Representative headless output

```text
client tick=0 seq=0 cargo=0/10 ore=20
client tick=1 seq=1 cargo=4/10 ore=16
client tick=2 seq=2 cargo=8/10 ore=12
client tick=3 seq=3 cargo=10/10 ore=10 event=MINING_STOPPED:CARGO_FULL
client tick=4 seq=4 cargo=10/10 ore=10
client tick=5 seq=5 cargo=10/10 ore=10
client tick=6 seq=6 cargo=10/10 ore=10
client tick=7 seq=7 cargo=10/10 ore=10
```

At 20 ms demo interval, observed maximum lateness in these runs: approximately 0.2–0.3 milliseconds per scenario. This is **not** a meaningful latency benchmark for full simulation loads, Windows, or multi-client workloads. The period defaults to 100 ms for a visually watchable demo.

## Limitations / Not yet complete

1. Native Windows builds and execution not tested. Windows .cmd script requires a built executable and Python 3; user-facing one-click installer does not exist.
2. Browser UI was tested through Node with mocked DOM/canvas. A genuine graphical browser rendering acceptance test is **not yet completed** here.
3. The SSE relay is loopback-only and view-only. A new HTTP `/events` connection creates a new disposable simulation run; it does **not** reconnect to an ongoing shared world. It does not supply production authentication, TLS, interest management, or command input.
4. Gap detection fails closed. The client must reload into a new run if the stream breaks; reconnect-to-same-world snapshot resync is a future gate.
5. Each step remains authoritative and SQLite-backed but fixed-step timer is synchronous and uses a single world. No scheduler overload/long-run benchmarks, cross-system simulation, NPCs, or actual 3D renderer.
6. Simulation positions/velocity are still integer millimeters per **logical tick**, not a calibrated continuous physical time scale. Tick duration must be deliberately chosen before production physics balancing.
7. `SGVIEW` uses FNV-1a consistency validation, **not cryptographic security**. The relay is not suitable for an untrusted remote connection.

## Running on Linux (development)

```bash
cmake -S . -B .build-live -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .build-live -j4
ctest --test-dir .build-live --output-on-failure
python3 tests/LiveEndToEndTests.py --executable .build-live/galaxy_live_scenario_host --relay live/relay.py
node --test tests/VisualContractNodeTests.js tests/VisualUiSmokeNodeTests.js tests/LiveProtocolNodeTests.js tests/LiveUiSmokeNodeTests.js
scripts/run_live_linux.sh
# open http://127.0.0.1:8765/viewer/live.html
```

`run_live_windows.cmd` provides an equivalent **developer** route after building natively with a compatible C++17 compiler and SQLite SDK. It uses the Windows Python launcher. The browser still cannot control the ship: this is live observation, not interactive gameplay.

## Next B4 gates

- Native Windows native build + actual Edge/Chrome rendering acceptance.
- Live-command input with conversation/session bound authority and explicit ownership checks, rather than a read-only demo.
- Real snapshot resynchronization to a running world after dropped updates and process reconnect.
- Simulation scheduler load/long-run benchmarks and calibrated physics duration.
- Smooth browser interpolation independent of fixed physics timestep.
- Optional stand-alone native launcher with no dependency on external Python for general users.
