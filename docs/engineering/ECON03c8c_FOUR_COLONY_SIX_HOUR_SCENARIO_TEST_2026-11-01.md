# Single Galaxy — ECON-03c8c four-colony six-hour accelerated scenario tests

**Date:** 2026-11-01 (local session).  
**Baseline:** Reconstructed Single Galaxy ECON-03c8b1 C++ prototype.  
**Status:** Regional scheduler, custody and fidelity-ownership **fixture pass**. **NOT a complete four-colony gameplay acceptance test.**

## Scenario setup

Two separate persistent regional scopes share one SQLite file per scenario, and each scope holds two colonies. All four colonies represent **1,000,000 residents each as aggregate cohorts** (not one million individually active NPCs). The near colony pair is colocated inside a region centered on 0 meters; the far colony pair resides in a region centered 50,000,000 meters away. Each region starts with a pair of actual `cold_colony::State` records managed by the game's native `regional_events::Scheduler`.

The simulated time rate is **5 logical ticks per game second**: six hours = **108,000 logical ticks**. The test makes one operation batch every **900 ticks**, 120 batches per scenario. One trade shipment in each direction between each pair is created per batch, with 250,000 kg of food, water, and oxygen per shipment; each shipment departs after 700 ticks and arrives 300 ticks after departure. Every colony also reserves and delivers **100,000 kg each of food, water, and oxygen** from its physical depot to its habitat every batch. Each arrival uses actual stock already present in that colony/region; mass remains conserved across depots, habitats, waste and in-transit custody.

To avoid shortages in this controlled interval, every colony begins with **4 million kg** of each life-support good in its habitat and **40 million kg** of each in its depot. Consumption is 1 kg per resident of each good every 9,000 logical ticks, a **test calibration**, not realistic final production/ration balancing. Colonies remain provisioned for the **six-hour window**, but **actual sustainable farming/oxygen recycling/mining/industrial production is not proven**. The regional engine does not currently autonomously mine, farm or manufacture when cold.

At time zero, the nearby pair is **detailed** and the far pair **cold**. “Detailed” is a separate in-memory copy of the same native regional scheduler held by a synthetic detailed fixture. **It is not the fully instantiated `PersistentWorld` with ships, NPCs, machines, combat and rendering.** Real `PersistentWorld` integration is tested separately in prior ECON-03c8b1 regressions.

### Scenario 1 — Visit the far colonies near the end

At **5 hours 51 minutes** (tick 105,300) teleport the synthetic player's observer from the near region to the far region, requesting near detailed→cold and far cold→detailed. The trip changes interest distance from 0 to 50 million meters and **does not** simulate physical ship flight or travel time.

### Scenario 2 — Visit halfway and return

At **3 hours** (tick 54,000), travel to the far pair and transfer near→cold / far→detailed. At **5 hours 51 minutes** (tick 105,300), return and transfer far→cold / near→detailed. Before the first transition, a deliberately injected SQLite `BEFORE INSERT` failure prevents one demotion from committing. The original detailed owner retains control; the pending transition is explicitly aborted, its generation revoked, and a fresh retry then succeeds.

## Measured outcomes

| Metric | Scenario 1 | Scenario 2 |
|---|---:|---:|
| Logical ticks completed | 108,000 / 108,000 | 108,000 / 108,000 |
| Game duration | 6 hours | 6 hours |
| Native trade shipments | 480 | 480 |
| Habitat supply transfers | 480 | 480 |
| Ownership flips (including initial activation) | 3 | 5 |
| Cold host commands committed | 600 | 600 |
| Stale owner commands rejected | 2 | 3 |
| Population represented | 4,000,000 | 4,000,000 |
| Resource shortages | 0 | 0 |
| Whole-test mass, incl. waste & in-flight | 528,000,000 kg | 528,000,000 kg |
| Region data changed/lost by transition | No | No |
| Checkpoint SQLite `quick_check` | ok | ok |
| Semantic equality digest (FNV-1a 64) | `7b17e3fe861d0479` | `7b17e3fe861d0479` |
| Wall-clock runner time | ~0.743 seconds | ~0.647 seconds |

The digest is a **diagnostic comparison**, not a cryptographic hash or independently authenticated proof. It covers the native scheduler's tick, population, stock/waste quantities, pending events, shipments and their identity/cargo/stage/timestamps, and pending notices. **The two scenarios reached the same final semantic regional state despite different observer travel patterns.**

At every travel event, **each pair had 500,000 kg food in transit** across the two simultaneously departing shipments (with equal water and oxygen cargo). The old/warm region was flushed to a new native checkpoint under an atomic owner-generation transfer; the new/detailed region was reconstructed from its previously durable cold checkpoint. Post-transfer snapshots were compared for exact semantic equality. After demotions, the cold region was closed and reopened and rechecked. Periodic cold restarts were also performed at 18,000-tick intervals. Updates from stale cold generations were explicitly denied.

All **8 targeted native regression tests passed** (regional c2-c7, first real-world bridge, and b1 reconciliation). Only this targeted regression subset was rerun during this new scenario test; **do not represent it as a new full 93-test run**.

## Important unpassed acceptance gates

1. **Autonomous mining and farming absent:** This scenario only exercises existing preloaded goods, **actual scheduled transfers**, aggregate life-support needs and trade shipments. It does not generate food from farms, ore from asteroids, energy or finished materials. A six-hour supply runway is not proof of indefinite sustainability.
2. **Detailed gameplay simulation absent:** A synthetic duplicate of the regional scheduler is used as the warm owner. Full rehydration of two 1M-population colonies into real spatial `PersistentWorld` factories, ships, named NPC agents and cached assets is **not established**.
3. **Physical shipment authorization absent:** Host commands use a conspicuous synthetic carrier/worker receipt accepted only inside the test policy. Ship paths, carrier loads, actual NPC work and real route disruptions are **not verified**. Trades are transfers of goods, not market price negotiation.
4. **Instant travel instead of flight:** The observer moves between region bounds without running the player's ship, flight time, or graphical LOD transitions. No display renderer is tested.
5. **Warm crash recovery absent:** Repeated cold-region persistence/restart and faulted handoff recovery passed, but the in-memory detailed fixture isn't itself fully checkpointed after every warm step. This does not establish safety across an arbitrary warm-process crash.
6. **Performance scope:** These fast tests are native headless accelerated simulations on the available Linux machine. They say nothing about 3D frame rate, network load, player machines, a trillion independent agents, or actual six-hour wall-time endurance.

## Reproduction

On the ECON-03c8b1 baseline, add the included `FourColonySixHourHarness.cpp` at `tests/FourColonySixHourHarness.cpp` and append the included `CMake_optional_target.txt` block to `CMakeLists.txt`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_FOUR_COLONY_SCENARIOS=ON
cmake --build build --target galaxy_four_colony_6hour_scenarios -j4
./build/galaxy_four_colony_6hour_scenarios ./scenario-output
ctest --test-dir build -R 'galaxy_(ECON03c[2-7]|econ03c8b_world_reconciliation_tests|econ03c8_real_world_bridge_tests)' --output-on-failure -j4
```

The runner writes `results.json`, two per-tick CSV files and separate SQLite files in the supplied output folder. The ZIP contains actual results, test source, CMake target fragment, database files, and verification logs.

## Engineering decision

**Regional checkpoint and ownership-flip mechanics have passed the two requested travel schedules at macro level.** The next genuinely end-to-end gate must integrate **per-colony production, inventory effects and ship-carrier receipts into the same event authority**, then rehydrate that physical state into a real detailed world. Do not claim full warm↔cold correctness until native factories, ships, NPC identity and gameplay renderer make this same roundtrip without loses or duplicates.
