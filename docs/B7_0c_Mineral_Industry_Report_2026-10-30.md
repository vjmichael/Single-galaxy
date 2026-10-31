# B7.0c — Mineral-Specific Extraction, Refining, and Usable Goods

**Single Galaxy · Engineering report · 2026-10-30 (Arizona date)**

## Delivered

- **Geological mining now generates physical mineral cargo** in integer kilograms, based on each asteroid's persistent, per-mille mineral composition. For a colony ship, mining a geological asteroid no longer silently produces anonymous ore. Generic small-ore extraction remains as a backward-compatible starter/legacy fixture.
- **10 processable, named mineral phases**, plus an unprocessed `other` fraction: serpentine, saponite, magnetite, pyrrhotite, carbonates, olivine, pyroxene, nickel–iron, plagioclase and troilite. Phase inventory is tracked by name and stored in a v9 canonical world snapshot.
- **7 usable/byproduct stock types:** structural alloy, aluminum, ceramics, glass, water, sulfur and carbon feed, plus tailings. Each mineral-processing operation conserves input mass (kilograms) across outputs and residue. These are **game-balance recipes** (not experimentally measured ore processing or a fully chemically balanced refinery).
- **Genuinely functional construction:** nickel–iron structural alloy or aluminum can be fabricated into 2-tonne construction modules; four modules build a persistent, player-owned outpost. Products such as water/ceramics/glass are physically recorded but **not yet wired to functioning life-support, agriculture, or glass-module recipes**.
- **SQLite-safe:** version 9 adds named physical stock records and new action types while retaining older snapshot readers. Mineral processing commands, receipts and resource movements are durable in the existing operation and resource ledgers. Recovery-copy rebuilding validates new command types. Existing outpost, station/market and gate fixtures remain intact.
- **Local browser controls:** read-only mineral/product inventory telemetry, selectable scanned asteroid target, mineral processing controls, structural-alloy/aluminum module fabrication. All actions go through a locally session-token-protected HTTP relay to server-authoritative C++ commands. The browser cannot invent resources, and mining still requires real proximity and ship capacity.

## Reproducible end-to-end material trace

| Process | Result | Retained consequence |
|---|---|---|
| Extract 12 tonnes from metallic asteroid | 10,320 kg nickel–iron mineral phase; 960 kg troilite and other minerals | Asteroid mass decreases by 12 tonnes |
| Process 10,320 kg nickel–iron | 9,288 kg structural alloy + 1,032 kg tailings | All input kilograms accounted for |
| Fabricate four alloy modules | Consume 8,000 kg alloy → four 2-tonne construction modules | Remaining alloy stays in cargo |
| Construct first outpost | Consume four modules | A persistent owned outpost physically embodies eight tonnes |
| Extract 10 tonnes from feldspar-rich stony asteroid | 4,000 kg plagioclase and 6,000 kg other phases | Profile is an **illustrative** feldspar-rich game generation case |
| Process 4,000 kg plagioclase | 800 kg aluminum, 2,400 kg glass, 800 kg tailings | Aluminum is a relatively easy, high-demand production material in game rules |
| Process 2,750 kg serpentine | 1,925 kg ceramics, 275 kg water, 550 kg tailings | Hydrated geology yields a useful volatile product |

**Unit distinction:** asteroid reserves and bulk ship capacity are in metric tonnes; phase-level cargo, goods, and tailings are in kilograms. Inventory sums to whole tonnes (1 tonne = 1,000 kg) at the extraction boundary. The world's `total_material_kg` remains constant across extraction/refining/fabrication/construction, without spawning matter. The model does not yet account for external processing energy/fuel, tool wear, vessel damage, or specific industrial machinery.

## Testing

Rebuild and test:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R 'galaxy_b70c_' --output-on-failure
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

New tests:
- `galaxy_b70c_mineral_industry_tests`: conservation, mineral fractions, process products/byproducts, failed resource requests, functional module-to-outpost, denied unauthorized mine, snapshot tamper rejection, pending-command upgrade, SQLite restart and recovery-copy.
- `galaxy_b70c_live_mineral_relay`: real host invocation, out-of-range mining, inventory message contract, authorized/unauthorized HTTP material requests, token/origin/schema validation.
- `B70cMineralIndustryNodeTests.js`: strict allowlisted mineral, goods and tailings telemetry parsing, rejects malformed/unknown/oversized records.

**Linux results:** 31 of 31 CTest suites passed in bounded groups or isolated reruns; 51 of 51 JavaScript tests passed. Prior live-host tests sometimes stall *when grouped*, and were rerun individually. The run grouping and exact test evidence should be kept distinct; no hanging run was counted as a pass. Source was also packaged without any build directory and smoke-built from a clean extraction.

## Explicitly not delivered yet

- Continuous streaming 3D flight; production still uses local bounded 2D positions.
- Player-facing alloy/ceramic/solar life-support, agriculture, and water-consumption systems (only structural construction modules are functional now).
- Full mineral assay by individual asteroid, variable subsurface veins, physical processing energy/heat/waste streams, and real planetary/aluminum ore distribution.
- NPC needs, grievances, farming, self-sufficiency assessment, managed absence or autonomous logistics.
- Browser *graphical click-through* validation and native Windows runtime validation; local HTTP and JS were tested, not a complete visual playthrough.
- Multiplayer authorization/security hardening; current relay is for local development only.

## Next small gameplay slice

**B7.0d — Agriculture and usable life support:** convert stored water, nutrients and ceramic/glass structural goods into functioning greenhouse/water/food supplies, add basic per-crew food consumption, and give the colony viability check measurable inputs. Keep NPC social politics and continuous-universe migration separate milestones.
