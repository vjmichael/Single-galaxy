# Single Galaxy — B7.0d Agriculture, Life Support & Viability

**Milestone:** B7.0d | **Date:** 2026-10-30 | **Status:** Linux implementation complete; **33/33 native/integration suites and 54/54 JavaScript tests passed**, production-style host build verified.

## What a player can do

The **colony-first** test world starts with exactly **one colony ship, 24 founding NPC crew**, and **no prebuilt stations, markets or stargates**. New worlds receive physically tracked starter stocks: **1,000 kg food, 3,000 kg water, 1,000 kg oxygen, 1,000 kg nutrients, and 1,000 kg captured CO₂**. These are explicitly seeded at world genesis, not replenished magically. Old saved colonies remain readable but do not retroactively receive free stocks.

New server-authoritative actions:

| Action | Input | Product / behavior |
|---|---|---|
| Plant crops | Per hydroponic batch: 2 kg water + 1 kg nutrients + 1 kg CO₂ | 3 kg growing biomass + 1 kg O₂; cannot harvest until 5 simulation ticks later; maximum 300 kg in beds |
| Harvest | 3 kg mature crop biomass | 2 kg food + 1 kg compost |
| Recycle compost | 1 kg compost | 1 kg reused nutrient, simplified game recipe |
| Recycle wastewater | 10 kg contaminated water | 9 kg recovered water + 1 kg sludge (integer truncation tracked) |
| Produce oxygen | 10 kg water | 8 kg oxygen + 2 kg stored hydrogen; game-balanced electrolysis |
| Load mineral-derived water | 1 metric tonne refined water stock | 1 metric tonne in life-support tanks, not a free spawn |

**Crew consumption:** Every 20 committed simulation ticks, each person consumes 1 kg food, 1 kg water and 1 kg oxygen. Consumed mass moves into tracked organic waste, wastewater and CO₂. Supply shortages increase a historical shortage count, **but do not instantly kill crew or manufacture a crisis**; individual NPC welfare and morbidity remain future features. A simulation tick is NOT a real-world day yet. Rates are tunable gameplay placeholders, not engineering measurements.

**Geology-to-life-support acceptance:** Mining **20 tonnes** of carbonaceous rock yields **11,000 kg serpentine** in this generation profile; processing that serpentine yields **1,100 kg water**. The ship can transfer **1,000 kg** to its life-support tank, leaving **100 kg** of tradeable/industrial water stock. The complete sequence preserves the world resource total and survives restart and journal-copy recovery.

## Viability check (deliberately conservative)

`assess_colony(ship)` exposes a **Survival** or **Stable** classification based on (1) at least three crew consumption cycles, (2) three consecutively fully supplied cycles, and (3) reserves for at least three more crew cycles. Historic shortages remain visible but do not permanently bar recovery. An NPC administrator can inspect the score.

Further enumerated stages (Livable, Self-Sufficient, Prosperous) are **reserved, NOT awarded** by this milestone. Simply having supplies, a successful harvest, or surviving while absent does not imply a colony can sustain quality of life, replace equipment, import essentials, or safely expand. True autonomy requires sustainable rolling supply-production balance, infrastructure repair and staffing, and quality-of-life measures in later milestones. **No automatic NPC production/colony expansion or individual Sims-style need bars** in B7.0d.

## Implementation and persistence

- C++17 `Simulation` and deterministic tick/update; strict authorizations and quantities.
- New `galaxy-kernel-v10` canonical snapshots; v1–v9 remain readable.
- New `lifesupport` records maintain every kilogram in a mass-balanced compartment. `total_ore` / `total_material_kg` include seeded living supplies without disturbing legacy test-world mass.
- New actions are journaled and verified in the SQLite `resource_ledger` with authoritative command ID, actors, amount and transformation type; copied-journal recovery replays actions. Versioned pending commands can be restored before execution.
- Local colony host emits `COLONY|1|...|LIFE` telemetry and Matrix Vision validates/displays the state. Local relay requires session token, matching origin, and allowlisted schemas. A production/hosted multiplayer identity model is *not* provided by this local demo.
- Updated older B7.0b test expects a v10 colony save when running the **current** provisioned live host (historical B7.0b source is unchanged).

## Verification and limits

- Focused native test: `galaxy_b70d_life_support_tests` — crops, maturation, crew cycles, positive and negative permissions, mineral-derived water, recovered world mass, canonical snapshots, starvation/recovery, SQLite/recovery-copy.
- Local integration: `galaxy_b70d_live_agriculture_relay` — real host planting/harvest/save/restart, relay authorization, schema rejections, viewer assets.
- Browser parser: `B70dLifeSupportNodeTests.js` — validates life telemetry and presence of UI controls.
- Full 33/33 CTest and 54/54 Node results: `docs/B7_0d_CTest_Final_Output.txt`, `docs/B7_0d_Node_Test_Output.txt`.
- Not yet verified: actual Chromium click-through, native Windows executable, continuous 3D space, physical greenhouse objects, crop varietals, individual NPC needs, automated farm jobs, planet ecology, closed-loop sustainability at large scale, or privately hosted server behavior.

## Start the local prototype

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R 'galaxy_b70d_' --output-on-failure
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

Navigate to `http://127.0.0.1:8766/viewer/interactive.html`. The Windows script remains source-only/unverified. An old B7.0c save intentionally loads without provisioned B7.0d life stocks; use a **new save path** to inspect the new starting scenario.

**Next bounded goals:** genuine farm equipment / energy integration, recurring NPC-managed agriculture instead of click chores, foods as usable goods, resource-viability with realistic rolling supply/demand, and later individual needs and happiness. The design is an accessible fictional game, not an industrial engineering simulator.
