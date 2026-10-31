# Single Galaxy — B5.4 Navigation / B6.0 Geography and Scanning

**Date:** 2026-10-30  
**Status:** Implemented and Linux-tested **incremental prototype**, not a complete galaxy-transfer or scanner UI.  
**Source branch:** isolated copy of B5.3; historical source and regression fixtures retained.

## What was completed

### B5.4: physical two-axis transport and route planning

- New `Navigation` C++ service provides a two-axis thrust/braking decision from authoritative ship position and velocity. Its output uses existing authorized `Action::Thrust` commands: no teleportation, instant gate warp or client position writes.
- Station B in the live-host test is now off-axis at (12,000, 7,000) debug millimeters. The verified 20-ore mining / load / undock / diagonal transit / dock / unload / sell journey ends with **100 player credits**, **20 ore added to Station B's market**, and its **credit reserve reduced from 1,000 to 900**.
- Corrected an approach waypoint-selection bug exposed by first diagonal test; new C++ tests cover diagonal, pure vertical and negative-axis approaches, physical docking, and restart recovery.
- Independent `GalaxyMap` supports system IDs/names, security score, sovereignty class, operational links and deterministic **shortest** vs **prefer-secure** routing. Closed links invalidate paths. These are *route plans*, not actual multi-system ship transfers.

### B6.0: first geography and scanning substrate

- Per-system catalog of celestial objects and signatures. Entries marked `mapped_on_entry` represent known major bodies only, not everything in a system.
- Directional scanning filters detectable contacts by heading, cone angle, and range; returns coarse bearing and range band, not exact warp coordinates.
- Owner-bound recoverable probes: deploy, sweep nearby hidden sites, recover, and reuse. Minimum sensor strength controls which sites can be resolved; multiple separated probe positions improve resolution, while duplicate co-located readings **do not** inflate confidence.
- Low-confidence probe results are quantized to a coarse candidate position; higher-confidence results may use the precise site coordinate in this initial model. Real noisy measurement/triangulation and occlusion are future work.
- `SurveyArchive` persists per-player site observations in its own SQLite database across restarts, with observation ticks, confidence, explicit public/private status, and an aggregated public view. Re-submitting old timestamps cannot replace newer measurements; one player counts only once per site in public confidence.
- Standalone `galaxy_survey_demo` exercises route options, entry bodies, a directional ship contact, probe deployment/recovery and publication to SQLite.

## Verified test evidence

| Gate | Result | Where |
|---|---|---|
| All C++ / Python-integrated CTest suites | **10/10 PASS** | `docs/B5_4_B6_ctest_output.txt` |
| Browser-contract and replay Node tests | **31/31 PASS** | `docs/B5_4_B6_node_output.txt` |
| Live transport via authoritative host + loopback relay | **PASS** (part of CTest) | `tests/B53InteractiveEndToEndTests.py`, `recordings/b54_off_axis_station_trade.sgview` |
| Secure/fast routes, blocked links, entry-map limitations | **PASS** | `tests/GeographyScanningTests.cpp` |
| Directional sensors, probe ownership, recovery, confidence | **PASS** | `tests/GeographyScanningTests.cpp` |
| Persistent archive, non-stale updates, publication privacy | **PASS** | `tests/GeographyScanningTests.cpp` |
| Physics-based navigation in 3 directions, save/restart | **PASS** | `tests/NavigationTests.cpp` |

Build and verify:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
./build/galaxy_survey_demo /tmp/single-galaxy-survey.sqlite
```

The live browser remains available using `scripts/run_interactive_linux.sh build/galaxy_interactive_host` and `http://127.0.0.1:8766/viewer/interactive.html`. The client still shows the **B5.4 two-station** physical transit; the new sensor subsystem is currently a C++ module/demo, **not** browser controls or live SGVIEW telemetry.

## Engineering boundaries and next gates

1. **B6.1 — Authenticated in-world scanning.** Attach system catalog and dynamic ship/asteroid positions to `PersistentWorld` or a trusted shared galaxy coordinator; expose bounded `DirectionalScan`, `DeployProbe`, `ProbeScan`, `RecoverProbe` as server-validated actions. Stream only observations to the relevant player's client; do not leak raw `GalaxyMap::sites()`.
2. **B6.2 — Durable multi-system jumping.** Build explicit gate entities and range/authorization/session-change checks; atomic transfer between systems/worlds preserving ship/cargo/wallet, transit guards, rollback/crash recovery, and system-scoped view resync. Route planning alone does not satisfy this.
3. **B6.3 — Survey network and economy.** Verified sensor submissions with player attribution, optional public-release policy, anti-duplicate credit payouts, time-aware confidence, history/freshness, privacy, publication permissions, and tamper/replay rejection.
4. **B6.4 — Living sensor world.** Dynamic contact visibility, signal strength/occlusion, triangulation, planetary/mineral survey, gas and anomalies, transient wormholes, NPC exploration vessels, and sensor-network sharing. A static site fixture is not an adequate final scanner.

## Explicit limitations

- **No inter-system jump is executable yet.** Systems, sovereignty and gates exist only as navigation/catalog metadata, not synchronized multi-world persistence or a player gate UI.
- **No live browser scan yet.** The map/sensors/archive have executable tests and a CLI demo, but are not attached to the interactive simulation's stream.
- **No credits are paid for surveys yet.** Public contributor attribution uses numeric player IDs in the data model, not a validated account identity/reward service.
- **No production authorization:** `SurveyArchive::record` is a **trusted server-side** method; a public HTTP API must *never* accept arbitrary client-supplied coordinates, ticks, or confidence.
- **No real-time browser rendering verified.** Node tests replay canonical state through client contracts; browser screenshots/Chromium and native Windows execution remain unverified.
- The simulator remains developer-oriented, with debug-scale distances and a single sample system for ship physics. These units are not final astronomical scales.

**Intent retained from design register:** geographic risk and control are independent; scanned knowledge persists after leaving a system; scans of changing objects can grow stale; entering a new system does not reveal hidden sites; scanning is a real skill of the player/equipment, not character XP; player data can eventually feed a public mapping network with provenance and rewards.
