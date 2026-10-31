# B3.2 — Visual Client Prototype: Execution Report

**Date:** 2026-10-30  
**Track:** Original single-fictional-galaxy simulator (not Eve-Mu / EVEmu)  
**Runtime verified:** Linux C++ / SQLite; Node.js contract and controller tests

## Scope

Built an original-shape, top-down HTML Canvas visual replay client using the existing B3 SGVIEW v1 protocol. Added a C++ `galaxy_visual_scenario_recorder` to generate time-sequenced frames from **committed steps** of the actual SQLite-backed world. Viewer opens locally with bundled recordings; no game engine dependency, remote network or proprietary assets.

This is a visual client integration milestone **B3.2**. The earlier backlog refers to future **B4** as an NPC economy; this visual work does not imply that NPC economy has begun.

## Results

| Gate | Test | Result | Evidence |
|---|---|---|---|
| B3.2-01 | C++17 Linux build | PASS | `verification/configure.txt`, `verification/build.txt` |
| B3.2-02 | Existing core/persistence/mining/client suites | PASS (4/4 CTest suites) | `verification/ctest_b4.txt` |
| B3.2-03 | 10 asteroids ×10, ship 100 | PASS: 10 removal events; 100 cargo; 0 asteroids | `recordings/ten.sgview`; C++ consumer output; Node test |
| B3.2-04 | 20-ore rock, 10 hold, 4/tick | PASS: cargo 4/8/10; one CARGO_FULL stop; 10 rock ore left | `recordings/hold.sgview`; Node test |
| B3.2-05 | Ship thrust and inertia | PASS: 12 ticks with updated position and persistent velocity | `recordings/motion.sgview`; Node test |
| B3.2-06 | C++ producer → independent C++ consumer | PASS: 3 scenarios | `verification/b4_scenario_cpp_tests.txt` |
| B3.2-07 | JS SGVIEW parser, consistency and order validation | PASS: 6/6 Node tests | `verification/b4_node_tests.txt` |
| B3.2-08 | JS UI/controller smoke with mocked canvas | PASS: 3/3 Node tests | `verification/b4_ui_smoke_node.txt` |
| B3.2-09 | Real browser visual rendering screenshot | NOT VERIFIED: local Chromium headless did not complete | `verification/chromium.txt` (diagnostic only) |
| B3.2-10 | Native Windows build / UI smoke | NOT RUN | Requires Windows environment |

## Concrete scenario states

**Ten rocks:** initial 10×10 ore and hold 0/100; after 10 committed mining ticks all ten rocks have been removed from the client projection, the ship has 100/100 ore and all ten `ASTEROID_DESPAWNED` activity notifications have been transmitted.

**Cargo limit:** initial rock 20 and hold 0/10; after the first three ticks the cargo sequence is **4, 8, 10**, rock ore sequence **16, 12, 10**; automatic job disappears at tick 3 and an activity records `MINING_STOPPED:CARGO_FULL`. Four later ticks do not transfer ore.

**Movement:** ship starts at (-3000,-800), is issued thrust commands at ticks 1 and 3, and its authoritative position and velocity are updated on subsequent ticks. The viewer draws a simple oriented ship shape from the received velocity and positional state.

## Safety / behavior

- No modification to original B3 simulation rules, EVEmu repositories, or historical EVE client.
- `galaxy_visual_scenario_recorder` creates **new** SQLite saves and refuses to overwrite existing files.
- Browser is read-only; it cannot cause state changes in the authoritative simulation.
- No third-party graphics/assets and no network calls in the viewer.
- All C++ view frames are emitted only after `PersistentWorld::step()` succeeds.
- Frontend rejects missing/out-of-order deltas and invalid projection hashes; importer rejects oversized files and unsafe integer conversions. This hash provides *consistency*, not security against malicious tampering.

## Known limitations / next gates

1. **Real browser validation on Windows**: open `viewer/index.html` and confirm canvas, play/pause, timeline, switching scenarios, cargo-full notice. Current evidence does not claim a fully rendered browser pass.
2. **Windows-native C++ and SQLite**: build MSVC x64, run all CTest suites and `scripts/windows_visual_recorder_smoke.cmd`; no Windows build is being claimed here.
3. **Live scheduling / transport (B3.1)**: fixed time-step producer, bounded update queues, client resynchronization/acknowledgments, safe cancellation. Current viewer is a deterministic offline replay, not a live server.
4. **Performance**: separate visual frame-time measurements from logical simulation tick and database commit timing, including realistic entity counts.
5. **B4 NPC economy**: defer until scheduler/durability requirements are satisfied. Keep all initial game work in one playable star system within the one-fictional-galaxy setting.
