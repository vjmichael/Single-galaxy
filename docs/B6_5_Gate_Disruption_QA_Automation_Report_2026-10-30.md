# Single Galaxy — B6.5 Gate Disruption, Durable Rerouting & QA-Only Input Automation

**Date:** 2026-10-30  
**Status:** Linux implementation and tests passed; production-browser, Windows and multiplayer anti-cheat verification outstanding.  
**Starting point:** B6.4 Lumen Reach seven-system region.  
**Design:** original game mechanics; no EVE source or proprietary assets copied.

## Delivered

1. **Durable bidirectional gate outage and repair:** A server-authority-only command updates both physical gate entities within one authoritative SQLite transaction. A command receipt and operation journal record it; recovery-copy replay reconstructs gate state. Incorrect endpoint pairs and ordinary player attempts are rejected. A gate closure does not cancel a jump already accepted before the closure.
2. **Route invalidation and replanning:** The live host monitors the *committed* world, not just a client graph. If any edge in an active route goes down, it computes a fresh route from the ship's current system to its requested destination. It reports `REROUTE`, new `LEG`, or `ROUTE_NONE`. Both shortest and safer policies remain supported. Route assistance never teleports the ship.
3. **QA-only input automation:** A local developer may start a test-enabled build with relay `--qa-test` and use the visibly marked QA panel to auto-run the selected route or induce/restore the test connection **2↔6**. QA auto-run uses the same ship undocking, acceleration/braking, and timed gate jumps as normal commands. It responds to the reroute when the gate goes down.
4. **Two distinct QA locks:** Build-time `SG_QA_AUTOMATION` (defaults to `BUILD_TESTING`) and runtime local relay `--qa-test`; the host requires the relay-provided test environment opt-in. Normal relay sessions reject QA HTTP actions with 403; even authorized test requests require same-origin, per-session token, exact fields, and bounded values. C++ host does not accept QA behavior unless compiled/enabled.
5. **Transparent audit:** The host logs `QA AUDIT` and emits `QA_MODE`, `QA_GATE`, `QA_STARTED`, `QA_STOPPED`, `QA_DONE`. Authorized testing is not treated as cheating; this is explicitly *not* hidden tracking, a production anti-cheat feature, or an accusation system.
6. **Browser-side QA controls:** Marked *TESTING ONLY* and hidden until the relay's server-side QA status is true. Sensor message parser validates gate/reroute/QA events; regional map reflects revised route.

## Native test evidence

`galaxy_b65_gate_disruption_tests` checks:

- player command path cannot impersonate a privileged gate action;
- wrong reverse endpoint is rejected;
- outage updates both physical endpoints only after a committed tick;
- shortest route no longer uses failed 2↔6 edge, safer policy selects a valid detour;
- conservation of ore and player cargo, no unintended relocation;
- injected SQLite write failure rolls back both database and memory, with no accepted receipt;
- new `PersistentWorld` loads outage after restart;
- separate `recover_copy` reconstructs the outage from verified operations;
- gate restoration restores the original shortest route and survives restart;
- a previously accepted jump still completes after a gate closes.

`galaxy_b65_live_gate_qa_e2e` runs the real C++ host and test input automation, interrupts the route, confirms an authoritative reroute and arrival at Far Lantern, restores the gate, checks the original return shortcut becomes available, validates scene snapshots against the jump ledger, and confirms cargo didn't change.

`galaxy_b65_relay_qa_controls` checks ordinary mode rejection, enabled QA action routing, schema strictness, Origin/token failure, server-origin QA events and client controls. JS tests validate the additional sensor records and reject malformed samples.

**Regression:** 23/23 CTest suites passed on Linux, executed in three bounded groups (1–14, 15–19, 20–23) to avoid concurrent long-host test contention; **45/45 Node tests passed**. An attempt to run multiple long-host tests together stalled; it is not counted as a pass. Full logs in `docs/B65_ctest_1_14.txt`, `_15_19.txt`, `_20_23.txt`, and `B65_node.txt`.

## Reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j4
ctest --test-dir build -I 1,14 -j2 --output-on-failure
ctest --test-dir build -I 15,19 -j1 --output-on-failure
ctest --test-dir build -I 20,23 -j1 --output-on-failure
node --test tests/*.js

# QA mode, exclusively on loopback, intentionally displayed and auditable:
python3 live/interactive_relay.py --host-exe build/galaxy_interactive_host --qa-test
# Open http://127.0.0.1:8766/viewer/interactive.html

# Production-style build: QA automation is compiled out.
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF
cmake --build build-release --target galaxy_interactive_host -j4
```

## Boundaries and next steps

- Only one locally brokered demo operator; normal multiplayer identity/authentication has **not** been built. A session token in a loopback demo is not production security.
- Gate mutation is an explicit server test/host authority, **not** something a remote player can legitimately request. Production gate management requires secure ownership/permissions, authenticated maintenance and possibly fuel economics.
- The QA control path is not a covert anti-cheat detector. Production cheat detection would require transparent, independently verified rules that distinguish legitimate assistive tools and QA from abuse.
- This milestone does not implement warp bubbles, PvP damage, gate construction, real-time cargo transfer across multiple separate server processes, or actual gate maintenance NPCs.
- Actual browser GUI rendering and Windows-native execution remain **unverified**. Tests exercise the C++ host, relay HTTP/events, parser and JS DOM contracts, not pixel-level rendering.
- Route changes take effect on **committed ticks**; in the same tick an already-authorized jump can finish, while a new jump ordered after a closure is denied by the source gate's authority.
