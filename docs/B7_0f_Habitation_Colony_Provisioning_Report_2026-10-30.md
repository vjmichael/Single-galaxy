# Single Galaxy B7.0f — Player-built habitation and local provisioning

**Status:** implemented and Linux-tested, 2026-10-30. This is a bounded, deterministic **2D** colony prototype, not the intended continuous 3D universe.

## What works

- Players construct a habitat **at an existing player-owned outpost**, for two physically manufactured structural modules (4 tonnes of embodied material). No starting buildings or construction freebies.
- Each habitat has **8 residential spaces** shared by on-site greenhouse staff and additional residents relocated from the colony ship. Crew transfers conserve the finite founding population; the original ship retains at least one pilot.
- Habitat residents require **physical food, water and oxygen** delivered by the ship at close range. They consume 1 kg of each per resident per 20-tick needs cycle. Farm workers continue using farm-local stores; housing does **not** double-charge their consumption.
- On a fully supplied cycle, the habitat records comfort/provisioning continuity. Staffing, capacity, stocks, cycles and shortages persist across SQLite restart and copied-journal recovery.
- The `assess_colony` view can reach `Livable` only after `Stable` basic needs, adequate housing for **all on-site farm workers and residents**, and at least **3 consecutive supported needs cycles in every occupied habitat**. A single comfortable site cannot conceal deficits elsewhere. Repeated good cycles allow recovery from a past shortage.
- Only locally owned and physically nearby outposts can receive housing commands. Transfers do not teleport materials or workers over interstellar distances. Overflow/invalid quantity/resource selectors are rejected.
- Matrix Vision adds local build, residency and stock-transfer commands and read-only habitat and viability telemetry, validated before display. The local relay enforces exact schemas, token and origin checks, and colony-mode authorization.

## Verified functional demonstration

The native test uses one colony ship with 24 crew and no initial infrastructure. It mines 50 generic starter ore units, refines and fabricates 25 construction modules, constructs a player-owned outpost, a two-module habitat, and a greenhouse. Three additional residents and five farming workers share eight residential spaces. Each role consumes its own local supplies, and sustained operations reach the `Livable` assessment. The ship travels away; persistent residents, workers, and their physical supplies remain at the outpost. Invariants checked: no new crew, no free mass, bounded capacity, no remote transfer, no forged ownership, durability across shutdown/restart, copied save recovery, and **tampered habitat receipt rejection**.

The live C++ host and Python relay test builds the same core sequence from textual commands, verifies residence records after restart, checks authorized commands and rejects malformed, forged or cross-mode inputs. It is not a completed graphical Chromium playthrough.

## Verification results

- **37/37 CTest suites have confirmed passing Linux runs** in bounded groups (tests 1–10, 12–19, 20–25, 26–34, 35–37, and the scanning test 11 run separately). The old scanning suite **failed once** due to insufficient contacts and **passed on its isolated rerun**; preserve this intermittent test concern rather than calling one uninterrupted full regression clean.
- **61/61 Node JavaScript tests passed** (58 existing + 3 new habitat tests).
- New B7.0f native/relay suites: **2/2 passing**, including SQLite resource-ledger tamper detection and recovery-copy verification.
- CMake **production-style Linux build passed** with `-DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF`. Clean-extraction ZIP verification passed: source rebuilt the B7.0f native test and colony live-host targets; both new CTest suites and the new Node contract checks passed from the extracted archive.

## Explicitly not implemented

- Full NPC identities, individual happiness/needs bars, recreation or relationship systems.
- Power generation and maintenance services for housing, comfort furniture/amenities, healthcare, and true dynamic quality-of-life simulation. `Livable` is currently a **minimal provisioning/habitation proxy**, not a guarantee of high citizen happiness.
- Closed-loop water, power, equipment replacement, budgets, institutional services, and therefore full colony `SelfSufficient`/`Prosperous` certification or autonomous growth under managed absence.
- Off-server catch-up while a private server is stopped; NPC governors and long-duration economic planning.
- Actual Chromium manual UI click verification; native Windows runtime; physically continuous 3D interstellar flight.

## Reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R galaxy_b70f_ --output-on-failure
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

Open the locally served `/viewer/interactive.html` page, build an outpost, then use its **B7.0f habitation** controls. Historical Lumen Reach stations and gates remain **test fixtures only**, not a released-game starting state.

**Next suggested milestone:** B7.0g dependable utilities + housing service capacity and a broader viability assessment, or usability work verifying a real browser/Windows session. Keep larger NPC social systems deferred until the core loop is reliable.
