## B7.0m — Player-governed policy assurance (Linux prototype)

Policy settings are now represented as durable, player-authorized standing mandates at a
player-owned outpost. Maintenance, first-aid care, community services and waterworks each
accept `None`, `Lax`, `Medium` or `Strict` assurance and a separate 0–3 assigned-inspector
headcount. Inspectors are existing housed residents and reduce remaining staffing capacity.
The new **POLICY_AUDIT** frame reports declared mandate, assigned versus required oversight
workers, and separate service operating evidence. A strict policy cannot manufacture its
own staff, materials, or successful operation. This is a capacity/assurance prototype:
inspections do not yet detect actual policy violations or issue penalties. B7.0m does not
implement immigration, individual NPC proficiencies, autonomous colony growth or a labor
market. The initial world remains a single colony ship with finite crew and zero settlements.

Run `ctest --test-dir build -R '^galaxy_b70m_' --output-on-failure` and
`node --test tests/B70mPolicyNodeTests.js`. See `docs/B70m_Policy_Execution_Report_2026-10-31.md`.

# Single Galaxy — B7.0l Geological Water Supply (current milestone)

**Linux-verified vertical slice:** physically mine water-bearing asteroid minerals, separate raw water, transport full 1,000 kg sealed cargo batches to player-built and powered local waterworks, produce 9 kg potable water and 1 kg retained brine per 10 kg raw input, and distribute potable water only to co-located habitat/greenhouse inventories. Exact mass balance, SQLite **v18** recovery, resource-ledger tamper checks, local relay authorization, UI telemetry and deterministic three-period supply diagnostics. Short-run availability **does not mean indefinitely renewable water** or autonomous expansion.

**Verification:** **51/51** native/integration CTest suites passed across four bounded groups, **81/81** Node.js tests passed, and production-style Linux compilation passed. No Windows-native or actual Chromium GUI execution was completed. Reproduce with `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4 && ctest --test-dir build -R '^galaxy_b70l_' --output-on-failure`, then `node --test tests/*.js`.

[Engineering report](docs/B7_0l_Mineral_Water_Replenishment_Report_2026-10-31.md).

---

# Single Galaxy — B7.0i Colony Resilience Audit (current milestone)

**Implemented / Linux-tested:** read-only, site-specific reserve and quality-of-life *proxy* audit; six-period physical food/water/oxygen reserves, three reliable power cycles, two local spare modules, bounded technician workload, and 25% recent-harvest margin. Typed read-only `RESILIENCE` local telemetry plus strict browser decoding and bottleneck explanations. An operational **candidate** is informational only; **autonomous growth remains disabled**. Existing SQLite v14 save schema unchanged.

**Verified:** 43/43 Linux native/integration suites and 69/69 JavaScript tests; production build and clean-source extraction verified. **Not implemented:** healthcare/rest services, measured renewable water, private-server pause/catch-up, NPC expansion/governance, continuous 3D flight or real Windows/Chromium tests.

**Reproduce:** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4 && ctest --test-dir build --output-on-failure -j4`; `node --test tests/*.js`. [Engineering report](docs/B7_0i_Resilience_Report_2026-10-31.md).

---

# Single Galaxy — B7.0h Delegated Maintenance (historical milestone)

**Implemented / Linux-tested:** Player-controlled physical deliveries of manufactured spare modules to built utilities; bounded resident technician assignments; persistent standing maintenance policies; autonomous repair of worn utilities while the simulation host runs; finite spares and equipment failure; typed local-interface controls and telemetry; SQLite snapshot v14 and audited spare-transfer ledger. **Neither player inactivity nor time passage generates resources or population.**

**Verified:** 41/41 native/integration tests, 67/67 Node.js tests, and a no-testing, no-QA production-style Linux build. The `delegated_maintenance_ready` check does **not** imply `autonomous_growth_ready`, which remains false. Windows-native execution, real Chromium interactions, true off-server catch-up and fully simulated NPC work schedules are outside this milestone.

**Reproduce:** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`; `ctest --test-dir build --output-on-failure -j4`; `node --test tests/*.js`. Launch `scripts/run_colony_linux.sh build/galaxy_colony_live_host` and open the local Matrix Vision page.

**Report:** [B7.0h Engineering Report](docs/B7_0h_Delegated_Maintenance_Report_2026-10-31.md).

---

# Single Galaxy — B7.0g Player-Built Utilities & Maintenance (historical milestone)

**Implemented / Linux-tested:** Players manufacture two structural modules and construct a **solar utility** at a player-built outpost. It has its own finite stored energy (up to 120 gameplay units), bounded solar generation, demand from a staffed greenhouse and an occupied habitat, deterministic equipment wear, outages, and physical repairs using one further manufactured module. A depleted store or failed generator interrupts farming and ongoing habitat comfort. Utility construction/repairs, condition, energy and service history persist in **SQLite snapshot v13** with audited resource-transfer receipts and journal-copy recovery. **No starting structures.** Older B7.0f saves and fixtures remain readable; their pre-utility provisional power assumption remains for historical compatibility.

**Measured:** **39/39** native/integration tests passed on Linux in bounded groups; **64/64** JavaScript tests passed. Both new CTest suites validate native electricity/failure/repair and the local live client/relay. Production-style Linux build (testing disabled) passed. **Not tested on native Windows or in real Chromium graphical interaction.** Full autonomous colony administration, self-sufficiency, individual NPC happiness, continuous 3D and true environmental solar modeling are not in scope.

**Local run:** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`; test `ctest --test-dir build -R galaxy_b70g_ --output-on-failure` and `node --test tests/*.js`. Run `scripts/run_colony_linux.sh build/galaxy_colony_live_host`. Build an outpost, then solar utility, farm and habitat. With a dedicated utility installed, local power matters; wear will eventually require a player-provided repair module. The colony viability check reports **utilities_sustained** after three consecutive powered consumption cycles at every occupied outpost, but **autonomous_growth_ready remains false**.

**Documentation:** [B7.0g Engineering Report](docs/B7_0g_Player_Built_Utilities_Report_2026-10-30.md) · [Consolidated Design](docs/SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md).

---

# Single Galaxy — B7.0f Player-Built Habitation & Provisioning (historical milestone)

**Implemented / Linux-tested:** construct a durable eight-slot habitat using two manufactured modules at a player-built outpost; physically deliver food, water and oxygen; relocate finite founding NPC residents; share habitation capacity with farm workers without double-consuming their supplies; operate household consumption every 20 ticks; assess sustained housing provisioning for a basic **Livable** milestone (not self-sufficiency); and save/recover all residential state through SQLite v12 with audited receipts. Matrix Vision has habitat controls, read-only status and local relay authorization. **No prebuilt infrastructure at colony start.**

**Verified:** **37/37** native/integration suites have passing Linux runs across bounded groups; one pre-existing live scanning suite failed once then passed on isolated rerun. **61/61** Node tests passed. A production-style Linux build succeeded without QA features. **No native Windows or real Chromium click-through verification.**

**Start:** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`; test `ctest --test-dir build -R galaxy_b70f_ --output-on-failure`; run `node --test tests/*.js`; launch `scripts/run_colony_linux.sh build/galaxy_colony_live_host`. Build an outpost before constructing the greenhouse or habitat, transfer supplies, and move residents in. Housing does not provide free food or certify autonomous growth.

**Engineering report:** [B7.0f](docs/B7_0f_Habitation_Colony_Provisioning_Report_2026-10-30.md). **Design reference:** [Consolidated feature spec](docs/SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md).

---

# Single Galaxy — B7.0e Player-Built Greenhouses & NPC Agriculture (historical milestone)


**Implemented / tested on Linux:** player-built greenhouse modules attached to player-owned outposts; local food, water, oxygen, nutrient and CO₂ inventories; station-based transfer of a finite NPC workforce from the original 24 colonists; automated timed crop production and site-specific worker consumption; no remotely teleported inventory or unlimited material generation; 3-cycle food-production-versus-demand check; SQLite v11 world snapshot, journal receipts and restart recovery; authenticated Matrix Vision greenhouse controls. This remains bounded **2D** rather than continuous 3D space. **Growth readiness is not yet certified.**

**Verification:** all **35/35** native/integration suites passed on Linux in bounded groups (test 11 separately) and **58/58** Node tests passed. Production-style/clean-distribution checks are separately documented in the engineering report. Browser Chromium clicking and Windows-native execution are unverified.

**Start:** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`; run `ctest --test-dir build -R b70e --output-on-failure` and `node --test tests/*.js`; launch `scripts/run_colony_linux.sh build/galaxy_colony_live_host` and open `http://127.0.0.1:8766/viewer/interactive.html`. New colonies start with **no prebuilt infrastructure**. Build an outpost, build a greenhouse, deliver supplies and staff it with real founding colonists. The farm produces while the server runs and resources remain.

**Report:** [B7.0e engineering report](docs/B7_0e_Player_Built_Agriculture_Managed_Crew_Report_2026-10-30.md). [Consolidated design](docs/SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md).

---

# Single Galaxy — B7.0d Agriculture & Life Support (historical milestone)


**Implemented on Linux:** finite starting food, water, oxygen, nutrients and captured CO₂ for a 24-person colony ship; mass-balanced hydroponic planting and timed harvest; compost/water recycling; oxygen production; real water imported from asteroid mineral processing; automatic crew consumption every 20 simulation ticks; **Survival / Stable** colony viability; canonical SQLite v10 journaling and recovery. There are still no starter stations or stargates. Life-support rates are explicit game-balance placeholders.

**Verification:** 33/33 native/integration suites passed in a clean serial Linux regression; 54/54 JavaScript tests passed; production-style Linux host build succeeded with testing disabled. Actual Chromium playthrough and Windows-native execution remain unverified.

**Test instructions:** `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4`; then `ctest --test-dir build -R galaxy_b70d_ --output-on-failure` and `node --test tests/*.js`. Start the interactive local demo with `scripts/run_colony_linux.sh build/galaxy_colony_live_host` and visit `http://127.0.0.1:8766/viewer/interactive.html`. Use a **fresh save path** for new provisioned colonies. This remains bounded 2D, not the final continuous universe.

**Scope guard:** `Livable`, `Self-Sufficient` and `Prosperous` are not certified by B7.0d. No automatic job/colony management, individual NPC happiness, or agriculture while the server is stopped. Windows/browser graphics not verified. [B7.0d engineering report](docs/B7_0d_Agriculture_Life_Support_Report_2026-10-30.md) · [Consolidated design](docs/SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md).

---

# Single Galaxy — B7.0c Mineral Industry (historical milestone)

**Implemented and Linux-tested:** geological asteroid mining now produces **named physical mineral cargo** rather than generic ore; refining separates **structural alloy, aluminum, ceramic, glass, water, sulfur and carbon feed** from **persistent tailings**, and structural alloy/aluminum becomes real construction modules usable in the first player-built outpost. All material is mass-accounted and recovered through SQLite v9 command/snapshot journals. B7.0b's legacy starter-ore path and historical Lumen Reach fixtures remain testable. **Current simulation remains bounded 2D**, not the intended continuous 3D universe.

**Testing:** 31/31 CTest suites passed on Linux in bounded groups or isolated execution; 51/51 Node tests passed. Real Chromium visual playthrough and Windows-native execution remain unverified. See [B7.0c report](docs/B7_0c_Mineral_Industry_Report_2026-10-30.md) and [consolidated design](docs/SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md).

**Local demo:**

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R 'galaxy_b70c_' --output-on-failure
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html`. Scan asteroids, fly physically within mining range, mine a selected geological body, process a named mineral (kg), fabricate alloy/aluminum structural modules and build. The local relay performs strict origin/token/schema checks. A construction module represents 2 tonnes. Starter small asteroids still supply generic ore for backwards-compatible early building.

**Next:** agriculture/usable water and food chains, crew needs and a first viability/progression check. These are not yet implemented.

---

# Single Galaxy — B7.0b Visual Colony Bootstrap and Large Asteroid Geology (historical)

> **Current game design (updated 2026-10-30):** See [Consolidated Feature Specification](docs/SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md). The final game starts with one colony ship and finite crew, has a continuous gate-free universe and **entirely player-built infrastructure**, broadens mining into agriculture, manufacturing, NPC quality of life and viable settlements, and supports managed absence. These are **design requirements, not features already implemented in B7.0b**.
> **Historical B7.0b roadmap:** B7.0c — mineral-specific inventory, real refining outputs, and functioning goods. Then agriculture/basic NPC needs and an explainable progression/viability assessment. Earlier B6 systems and stargates are test fixtures.


**Current deliverable (Linux-tested):** one colony ship with 24 simulated crew, *no starter stations or gates*, local Matrix Vision controls for scan, physical thrust, mine, refine, fabricate and construct the first owned outpost; three persistent geological asteroids with approximate radii, bulk density, million-tonne reserves, and **illustrative mineral compositions using actual mineral names**. The client receives validated read-only geological telemetry and authoritative SGVIEW frames. This is still a local bounded **2D prototype**, not continuous interstellar 3D space.

**Verification:** 29/29 native and integration CTest suites passed in bounded groups (tests 1–10, test 11 isolated, tests 12–19 and 20–29); 50/50 Node tests passed. A clean-extraction build test is described in `docs/B7_0b_Colony_Visual_Geology_Report_2026-10-30.md`.

**Local Linux demo (requires CMake, C++17 compiler, SQLite3 developer files, Python 3 and a browser):**

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
# Open http://127.0.0.1:8766/viewer/interactive.html
```

The local simulation is persisted to `~/.local/share/single-galaxy/colony.sqlite` by default. The `--colony` mode is separate from the older prebuilt Lumen Reach fixture. **No real browser visual pass has been claimed:** headless Chromium navigation to the local relay was blocked here with `net::ERR_BLOCKED_BY_ADMINISTRATOR`. Windows startup script is supplied but Windows-native execution has not been validated.

**Scientific scope:** C/S/M inspired rock categories use real names (including serpentine, saponite, magnetite, olivine, pyroxene, plagioclase, troilite and nickel-iron), but the numerical mineral shares are invented game templates, not measured assays. The current refining recipe still transforms *generic ore*, not mineral-by-mineral yields. The body masses/densities are illustrative and no real asteroid catalog is imported. Original work; no EVE source/assets.

See [B7.0b engineering report](docs/B7_0b_Colony_Visual_Geology_Report_2026-10-30.md).

---

# Single Galaxy — B6.6 Durable Stargate Operations (historical fixture)

**New:** version-6 authoritative SQLite snapshots persist gate-pair condition, maintenance labor and spare parts. Every 10 committed ticks, operating links wear and staffed maintenance consumes supplies. A failed gate closes both directions and forces rerouting; it stays closed until repaired and explicitly restarted. The live client receives bounded read-only status for gates in the current system, while QA-only input automation stays separate from the future player autopilot. See [`docs/B6_6_Gate_Operations_Report_2026-10-30.md`](docs/B6_6_Gate_Operations_Report_2026-10-30.md).

**Linux verification:** 25/25 CTest suites (segmented for long live tests), 47/47 JavaScript tests, focused ASan/UBSan pass. Windows and actual graphical playtest unverified. **Caution:** technician counts and administratively provisioned spare parts are an operating model, *not yet* real NPC workers/physical supply-chain inventory.

---

# Single Galaxy — B6.5 Dynamic Gate Disruption + QA-Only Automation (historical fixture)

**B6.5 Linux-verified extension of B6.4 Lumen Reach.** Live gate connections may be disabled/repaired in a single durable, journaled, server-authority operation. Active route plans detect closed links and produce a new shortest/safer route using actual physical gate status. The local demo has an **explicit QA-only input automation** mode for multi-hop navigation and gate-disruption testing. It performs real thrust and authorized jumps; no teleportation.

**Testing opt-in:** `python3 live/interactive_relay.py --host-exe build/galaxy_interactive_host --qa-test`. The QA panel is hidden in ordinary sessions, requests are rejected outside QA mode, and the test host emits visible QA indicators/audit records. The feature additionally requires a test-enabled build (`-DBUILD_TESTING=ON`, `SG_QA_AUTOMATION=ON`). **Never treat QA automation as proof of cheating or covertly identify a player.** Production-style builds use `-DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF`.

**Verification:** 23/23 native/integration CTest suites in three bounded groups, 45/45 Node tests. Live QA journey verifies gate closure/reroute/repair, unchanged cargo and consistent SQLite jump ledger. Recovery-copy and injected transaction failure also tested. See [`docs/B6_5_Gate_Disruption_QA_Automation_Report_2026-10-30.md`](docs/B6_5_Gate_Disruption_QA_Automation_Report_2026-10-30.md).

**Not claimed:** Windows-native or actual browser-rendering verification; production multiplayer authorization; sophisticated anti-cheat; player-built gates and NPC maintenance.

---

# Single Galaxy — B6.4 Lumen Reach: Seven-System Region (historical fixture)

**B6.4 Linux-verified prototype.** An entirely original seven-star-system region (Lumen Reach) with nine bidirectional stargate links (18 real world gate entities), NPC/unclaimed/player sovereignty and independently modeled security ratings, a branching route graph, a live browser starmap, physically flown multi-hop travel, and system-specific scanning and personal survey history.

**What's new:**

- The original two-system Haven ↔ Sable Gap fixture is retained for older B6.2/B6.3 compatibility.
- Five additional systems contain their own station, asteroid deposit, mapped star and planet, and hidden gas/anomaly sites. Resource quantities are currently small **test fixtures**, not final realistic asteroid masses.
- A **shorter hazardous** path from Haven to Far Lantern is 1→2→6→7 (three jumps). A **safer, longer** path is 1→3→4→5→7 (four jumps). Route selection is advisory; it does not teleport or secretly jump.
- The **Assist to next gate** action uses a C++ physical steering controller to the next planned edge; **Jump planned leg** independently invokes the simulation's distance-checked, authorized, timed jump transition.
- Each committed jump replaces the client scene with a destination-only snapshot; surveys and archives remain scoped to the actual current system.
- `RegionState::operational_routes` derives live gate availability from the actual authoritative gate entities, not a separate client-supplied map. A gate-down reroute is tested on the graph; **authorized runtime gate outages and persistent player gate construction remain future work.**

**Rebuild and test (Linux):**

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j2 --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open <http://127.0.0.1:8766/viewer/interactive.html>. Select a destination from the map, choose **Shortest** or **Safer**, and press **Plan route**. Undock, use **Assist to next gate**, then **Jump planned leg** when close enough to the physical gate. The star chart highlights your current system and planned path. A local development session defaults to 600 ticks at 100ms per tick; this is not a persistent server service.

**Engineering evidence:** [`docs/B6_4_Lumen_Reach_Region_Report_2026-10-30.md`](docs/B6_4_Lumen_Reach_Region_Report_2026-10-30.md). New tests verify the 18-gate topology, route alternatives and disconnected graph, a 1→2→6→7 journey, three completed durable jump ledger rows with unchanged cargo, survey isolation, parser bounds, and the route controls. Full regression results are recorded in the adjacent logs.

**Not yet verified/implemented:** native Windows run; actual browser GUI rendering in a Chromium session; offline reopening of a finished interactive demo (the host currently insists on a fresh temp save); sustained multiplayer authentication; warp travel, gate-building, mutable gate outages from authenticated clients, NPC patrols, advanced material economies, and a live public survey network.

---

## Previous milestones

# Single Galaxy — B6.3 Live Routing and Survey History (historical fixture)

**Implemented and tested on Linux:** a route advisory from the live C++ gate network (shortest/safer), browser controls with strict input validation, append-only private survey revisions, and latest-observation restoration when returning to a previously explored system. The original authenticated SGVIEW projection, cargo, jump, and market code remain intact. **This does not perform automatic jumps**, and the live world currently has two star systems.

**Verification:** 18/18 CTest suites and 38/38 JavaScript tests passed on Linux. See [`docs/B6_3_Routes_Survey_History_Report_2026-10-30.md`](docs/B6_3_Routes_Survey_History_Report_2026-10-30.md) and raw logs.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j2 --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html`; use **Plan route** to view the server's advisory; **Assist to Gate** and **Jump Gate** remain distinct operations. The **Observation history** button reads up to 100 revisions in the current system. Prior observations are labeled with their original simulation ticks. The public online survey service, multiplayer authentication, browser visual validation and native Windows execution are not implemented/verified.

---

# Single Galaxy — B6.2c Live Multi-System Navigation (historical fixture)

**Implemented and tested on Linux:** real stargate jump commands from the local browser client, authoritative C++/SQLite ownership transfer between two star systems, strict system-specific SGVIEW snapshots, jump countdown and arrival-cloak telemetry, and scanning that follows the ship into the destination system. Roundtrip carries and sells physical ore (20 ore → 140 credits). **No EVE assets or protocol code were copied into this original game.**

**Tests:** 16/16 CTest suites, 35/35 JavaScript tests (Linux). See [`docs/B6_2c_Live_Multisystem_Navigation_Report_2026-10-30.md`](docs/B6_2c_Live_Multisystem_Navigation_Report_2026-10-30.md), logs, and reproducible [`recordings/b62c_live_roundtrip.sgview`](recordings/b62c_live_roundtrip.sgview).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j2 --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html` on the same machine. **Jump Gate** activates the nearby gate if authorized and in range. **Assist to Gate** moves and brakes the ship physically. The viewer shows jump transit, arrival cloak, local entities and sensor results. For user-provided EVEmu client/server research and a structurally repaired CSV, see [`research/README.md`](research/README.md). Native Windows/real browser rendering remain unverified.

---

# Single Galaxy — B6.2b Durable Multi-System Travel (historical fixture)

**B6.2b implemented and Linux-tested.** An authoritative C++/SQLite world now holds multiple star-system partitions. Real ship entities cross persistent stargates via validated, timed commands while retaining owner, inventory, identity and market conservation. Each accepted jump has durable departure and arrival ledger entries; restart, rollback, tamper detection, return travel and recovery-copy are tested. **B6.2c (multi-system browser rendering and controls) is NOT implemented.**

**This execution:** 13/13 CTest suites; 33/33 Node tests; focused Clang ASan/UBSan passed. For evidence, design decisions, caveats and the next gate see [`docs/B6_2b_Durable_Multisystem_Travel_Report_2026-10-30.md`](docs/B6_2b_Durable_Multisystem_Travel_Report_2026-10-30.md).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
./build/galaxy_b62b_durable_jumps_tests
./build/galaxy_multisystem_demo /tmp/fresh_galaxy_b62b.sqlite
```

---

# Single Galaxy — B6.1 Live Scanning + B6.2a Gate State Kernel (historical fixture)

**B6.1 implemented and tested on Linux:** Directional and recoverable probe scanning from the actual committed ship position; hidden-site discovery, known star/planet on entry, local private survey archive and explicit publication; interactive browser controls and strict separate server-origin sensor events. **B6.2a implemented only as a headless testable gate-transition state machine** (no live multi-system jump yet). Details in [`docs/B6_1_B6_2a_Live_Scanning_Gate_Transitions_Report_2026-10-30.md`](docs/B6_1_B6_2a_Live_Scanning_Gate_Transitions_Report_2026-10-30.md).

Linux verification: **12/12 CTest suites, 33/33 Node tests.** Browser/Windows native runtime tests not verified. All prior B5.4/B6.0 work is preserved below.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html`. Undock before scanning. All survey results belong to the single demo operator; this local relay is **not** a multiplayer privacy boundary. Temporary relay saves are deleted at shutdown.

---

# Single Galaxy — B5.4 Navigation + B6.0 Geography/Scanning (historical fixture)

**Implemented and Linux-tested:** B5.3 physical two-station ore transport now supports **two-axis navigation assist**, verified on an off-axis destination; a separate C++ geography/sensor module introduces a multi-system route graph, a security-versus-sovereignty distinction, directional contacts, recoverable survey probes, hidden-site resolution, and **persistent private/public survey records**. See [`docs/B5_4_B6_0_navigation_geography_scanning_report_2026-10-30.md`](docs/B5_4_B6_0_navigation_geography_scanning_report_2026-10-30.md).

**Regression:** 10/10 CTest suites and 31/31 Node tests passed on Linux; source, replay and execution logs included. **Not yet implemented:** actual inter-system gate jumps, scanner controls in the live browser, survey reward payouts or online global data service. Windows native execution and real-browser rendering are unverified.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
./build/galaxy_survey_demo /tmp/survey_demo.sqlite
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

---

# Single Galaxy — B5.3 Two-Station Logistics (historical fixture)

**Implemented, Linux-tested prototype:** Stage ore in Station A's private hangar; load it into your ship; undock and physically travel to Station B; dock, unload and sell ore at a station-specific, finite-funds bid. Market purchases move physical ore into the station market stock, and credits transfer from that station's existing reserves into the player's wallet. All state changes remain C++/SQLite-authoritative. The UI provides optional thrust-based travel assistance, not teleportation or a stargate.

**Automated verification (Linux):** 8/8 CTest suites + 30/30 Node tests. See [`docs/B5_3_station_logistics_execution_report_2026-10-30.md`](docs/B5_3_station_logistics_execution_report_2026-10-30.md) and the corresponding execution logs. **Native Windows and actual Chromium rendering are not yet verified.**

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html` while the local relay is running. Workflow: **Undock → Mine → Dock → Unload → Load → Undock → Assist to Station B → Dock → Unload → Sell**. Station A's bid is 2 credits/ore; B's bid is 5 credits/ore. Both start with 1,000 credits; owner 7's wallet starts at zero. B5.3 remains **within one system**. See the [living brainstorming register](docs/SingleGalaxy_Brainstorming_Ideas_2026-10-30.md) for future features, which are not implemented here.

---

# Single Galaxy — B5.2 Live Station Client

**NEW B5.2:** The development relay now starts a ship docked at its local station. In the live 2D browser, click **Undock → Mine 4/tick → Request dock → Unload ore**. The authoritative C++ simulation validates requests, commits the resulting state to SQLite, and streams dock state and per-station hangar inventory alongside cargo. The local page now shows docking transitions, command results, and actual hangar contents.

On Linux, run:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Then open `http://127.0.0.1:8766/viewer/interactive.html` on the same machine. See [B5.2 execution report](docs/B5_2_execution_report_2026-10-30.md), and the [brainstorming decision register](docs/SingleGalaxy_Brainstorming_Ideas_2026-10-30.md). The prototype is local-only. **No Windows-native or actual Chromium rendering pass has been claimed.**

---

# Single Galaxy — B5 Station Lifecycle Acceptance Tests

**NEW B5:** The ship starts docked when created with `Simulation::spawn_ship_docked`. `RequestUndock` and `RequestDock` are authorized and complete after two further simulation ticks. Each station offers its own private, per-player hangar. `UnloadOre` moves only what fits at that exact station; remaining cargo stays aboard. Both requested numerical scenarios, plus restart, rollback, recovery, access-control, and client-event tests pass.

**Run the new tests (Linux):**

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/galaxy_station_lifecycle_tests
```

See [`docs/B5_station_lifecycle_report_2026-10-30.md`](docs/B5_station_lifecycle_report_2026-10-30.md) for all expected values and limits. These are **headless acceptance tests**; the B4 browser controls remain unchanged for the next integration gate, and Windows-native verification is still pending.

---

# Single Galaxy — B4.3 Interactive Matrix Vision

This is a local developer prototype for an **original** fictional-galaxy space game, currently simulating one star system. It does not depend on or modify EVE Online / EVEmu code or client assets.

**New:** a browser can issue bounded thrust, start-mining and stop-mining commands to **one shared authoritative C++ simulation**. SQLite commits happen before SGVIEW updates are sent to all viewers. The Canvas presentation smooths ship movement without modifying simulated positions. Reconnecting browsers replay the same bounded world history rather than create another world.

## Linux developer build

```sh
cmake -S . -B .build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build .build -j4
ctest --test-dir .build --output-on-failure
python3 tests/InteractiveEndToEndTests.py --executable .build/galaxy_interactive_host --relay live/interactive_relay.py
node --test tests/*.js
scripts/run_interactive_linux.sh
```

Open **http://127.0.0.1:8766/viewer/interactive.html**. Click **Mine 4/tick**; observe cargo reach 20/20 and stop automatically, then use directional thrust buttons to move the ship. **Drift** sets no additional acceleration, preserving inertia. You can open a second browser tab or choose **Reconnect viewer** to observe the *same* running simulation.

## Windows developer instructions (NOT verified)

Native Windows requires an MSVC C++17 SDK and a suitable SQLite SDK (or verified vendored amalgamation). Build with CMake, then use `scripts/run_interactive_windows.cmd`. The helper uses Python 3 **only for development**, not a finished installer. Linux tests do not substitute for native Windows validation.

## Documentation and tests

See `docs/B4_3_Interactive_Contract.md` for the command trust boundary and synchronization guarantees, plus `docs/B4_3_execution_report_2026-10-30.md` for actual results and limitations.

The previous read-only B4 demonstration still exists in `viewer/live.html`, `live/relay.py`, and `galaxy_live_scenario_host`. Recorded scenarios and all original simulation, SQLite recovery, mining lifecycle, and client contract tests are retained.

## Development-only limits

The local command token is anti-CSRF protection, not player authentication. One active owner is hardcoded inside the host; do not expose the Python HTTP relay outside loopback. The world lasts 180 demo ticks by default, and reconnect uses bounded frame replay. It is **not** a durable save slot, production multiplayer service, native Windows release, or 3D game.


## B7.0j — Community-service vertical slice (2026-10-31)

See `docs/B7_0j_Community_Services_Report_2026-10-31.md` for verified Linux implementation: constructed community halls, physical social-meal/water provision, allocated resident attendants, recurring energy and service continuity, v15 durable state and independently checked resource ledger. The hall is evidence of local leisure access, **not** a medical clinic or automatic prosperity. Autonomous growth and server-offline time skips remain unimplemented.


## B7.0k basic clinical care (Linux prototype)

This bounded 2D colony simulation now supports physically built first-aid clinics with real water and nutrient-based substrate inputs, housed NPC medic staffing, shortage-triggered aggregate cases, actual consumptive treatment and waste conservation. SQLite v16, independent resource-ledger validation, local relay allowlisting and separate CLINIC/CLINIC_ASSESS telemetry are implemented and tested. These are basic first-aid game mechanics, **not** individual diagnoses, medicines, fully self-sustaining life support, autonomous growth, or a released 3D game. See `docs/B7_0k_Clinical_Care_Report_2026-10-31.md`.

### B7.0k-2 — local water/oxygen reclamation (2026-10-31)

B7.0k clinical care now has a second subgate: a physically constructed, solar-powered local water/CO₂ reclamation unit, mass-conserving waste byproducts, 3-period throughput/demand histories and conservative multi-site diagnostics. `LIFE_RECOVERY` is read-only and **does not certify independent water renewal or autonomous growth**. The source, verification details and known limitations are in `docs/B7_0k2_Life_Reclamation_Engineering_Report_2026-10-31.md`.

### B7.0n foundation — people and simulation scale (2026-10-31)

Individual NPC records can be explicitly created at **genesis only** via
`Simulation::enable_individual_workforce()`. They provide durable 24-person
identities, ship/farm/habitat location, productive farm XP and strictly
one-to-one farm mentorship. The legacy aggregate staff/needs accounting remains
authoritative, and earlier saves are not silently converted. Current snapshot
format for opted-in worlds: **v20**. See
`docs/B70n_Persistent_Workforce_and_Cohorts_Report_2026-10-31.md`.

The independent `PopulationCohorts` module keeps integer person counts in
occupation / skill / expectation strata. Its trillion-scale arithmetic test is
**not** a trillion-NPC runtime benchmark or a complete colony simulation.
