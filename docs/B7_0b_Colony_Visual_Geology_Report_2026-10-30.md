# Single Galaxy — B7.0b Colony Visual Client and Large Geological Asteroids

**Engineering execution report — October 30, 2026 (local environment, Linux)**

## Goal and scope

Advance the *one starting colony ship and finite NPC crew, no prebuilt infrastructure* game loop from headless commands to Matrix Vision's local client. Add long-lived, very large asteroid bodies carrying scientific-sounding but explicitly *fictional* mineral inventories. Retain the original C++/SQLite authoritative simulation, deterministic replay, rollback, and older test fixtures. This is the *next vertical slice*, not yet the proposed continuous 3D galaxy.

## Changes implemented

1. **Colony local live host:** `src/colony_live_host.cpp` initializes one player colony ship, crew=24, two nearby small mineral-bearing starter deposits and three large geological asteroid bodies. No stations, gates or prebuilt infrastructure. A saved colony resumes rather than being reset on launch. Server-authorized `SCAN`, `MINE`, `REFINE`, `FABRICATE`, `BUILD`, and `THRUST` enter the C++ simulation; the host emits ordinary SGVIEW frames and validated `COLONY|1|` telemetry.
2. **Large asteroid geology:** `Simulation::add_geological_asteroid` stores rock class, approximate radius, density, original tonnes, and persistent remaining tonnes. Starting example classes are Carbonaceous (radius 70 m, density 1700 kg/m³, approximately 2.44 million tonnes), Silicate (radius 95 m, density 2900 kg/m³, approximately 10.41 million tonnes), and Metallic (radius 55 m, density 5700 kg/m³, approximately 3.97 million tonnes). Bulk mass uses the ideal sphere formula `4/3 × pi × radius³ × density / 1000`; actual asteroids are irregular and more complex.
3. **Mineral profiles:** Carbonaceous examples include serpentine, saponite, magnetite, sulfides, carbonates; rocky silicate examples include olivine, pyroxene and plagioclase; metal-rich examples include nickel-iron and troilite. Templates total 1000 permille per asteroid and are exposed as a read-only estimate. The percentages are **not** measurements of Bennu or any catalogued body, and are not yet separate mineable products.
4. **Mining and accounting:** Mining operates within configured range of an asteroid's *surface*, not its distant center. Removed bulk resource tonnes enter cargo and are preserved in conservation accounting. Large rocks do not vanish after tiny yields; the persistent reserve decreases exactly by the mined amount. All original mass continues to be accountable through the elementary onboard recipe and outpost build chain.
5. **Snapshot persistence:** Snapshot serialization/deserialization adds canonical `galaxy-kernel-v8` geology metadata and rejects malformed records. Fixed an older construction/maintenance path that downgraded snapshot version and silently removed geology after an outpost was constructed. Outpost ownership also now appears in SGVIEW; prior contract validation allowed only test-fixture owners.
6. **Matrix Vision controls:** `viewer/interactive.html`, `viewer/interactive.js`, `viewer/colony.js`, `viewer/viewer.js` add a colony-only panel with command controls and geology readouts, bound to streamed authoritative state, with schema validation and no local simulation overrides.
7. **Local relay security:** `live/interactive_relay.py --colony` and optional `--save` keep colony-start action allowlist separate from old gate-navigation and QA commands. Test confirms wrong token, cross-origin, malformed quantities, and attempted QA/gate actions are rejected. This is a loopback developer relay, *not* a production-grade multiplayer authentication service.

## Test evidence

| Group | Result | Evidence |
|---|---|---|
| Native tests 1–10 | 10/10 passed | `docs/B70b_ctest_1_10_then_interrupted.txt` (test 11 started without completion in grouped run) |
| Native test 11, individually | 1/1 passed | `docs/B70b_ctest_11_isolated.txt` |
| Native tests 12–19 | 8/8 passed | `docs/B70b_ctest_12_19.txt` |
| Native tests 20–29 | 10/10 passed | `docs/B70b_ctest_20_29.txt` |
| JavaScript / parser / interface contract | 50/50 passed | `docs/B70b_node_50_tests.txt` |

**Unique total: 29/29 CTest suites, 50/50 Node tests passed**, completed as bounded groups/individual tests rather than claiming one uninterrupted all-suite invocation. The initial long group stalled at test 11, which passed alone. Focused new cases `galaxy_b70b_geology_tests` and `galaxy_b70b_live_colony_e2e` passed, including live SQLite restart and relay request validation. The program compiled fully on Linux.

## Run or reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -I 28,29 -j1 --output-on-failure
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
# View at http://127.0.0.1:8766/viewer/interactive.html on the same machine
```

The Linux local helper starts a development relay, save path `~/.local/share/single-galaxy/colony.sqlite`, tick rate 100 ms and demo life 1200 ticks. The Windows helper is included but not verified on a native Windows host.

## Scientific references and limitations

References used for **qualitative inspiration**, not direct compositional measurements of generated rocks:

- NASA, *Asteroid Facts*, overview of carbon-rich, stony and metal-rich classes: https://science.nasa.gov/solar-system/asteroids/facts/
- Bennu sample analyses reported real phyllosilicates, magnetite, sulfides, carbonates and other minerals; see a published 2025 analysis https://www.nature.com/articles/s41561-025-01741-0 and NASA's sample discussion https://www.nasa.gov/solar-system/surprising-phosphate-finding-in-nasas-osiris-rex-asteroid-sample/

This current asteroid implementation is a simple **sphere with bulk-density mass estimate**, fictional mineral-percentage estimates, and generic mined tonnes. There is no measured mineral-grade heterogeneity, mineral-specific cargo, metallurgy/reagent requirements, seismic prospecting, deposit stratification, asteroid rotation, realistic irregular shape, orbital mechanics, true mass-tuned collision geometry, provenance-tagged real catalog import, or resource market yet. Mineral class profiles are *model assumptions* and can be updated as later research warrants. Starting rocks are 2.44–10.41 **million** tonnes, not yet the billion-tonne industrial-scale bodies the user envisions.

**Still outstanding:** Continuous 3D streaming universe and interstellar flight; full 3D ship piloting; per-NPC identities; genuine player-built gate networks; ore mineral separation and distinct refining yields; production Windows installer or actual Chromium playtest. Attempt to load the local test relay in installed Chromium through Playwright returned `net::ERR_BLOCKED_BY_ADMINISTRATOR`; no graphical rendering validation is claimed. The automated protocol and static viewer-contract tests do not establish successful on-screen playability.

## Next practical step

B7.0c: connect mineral profiles to **physical separated cargo inventories** (e.g. actual recovered silicates, Fe/Ni minerals and sulfides) with mass-balanced processing, variable recoverable grade, and explicit tailings/recycling, then connect these inventories to first industrial construction recipes. Keep the first colony ship self-sufficient and preserve depletion and restart behavior.
