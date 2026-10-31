# Single Galaxy — B7.0k-2 Local Water/Oxygen Reclamation

**Date:** 2026-10-31  
**Status:** Implemented and independently Linux-tested (not Windows-native).  
**Scope:** B7.0k continuation after clinical care: player-built local recovery, recent per-period accounting, and conservative certification.

## What changed

1. A colony ship owned by the player constructs one **reclaimer** at an existing owned outpost, in local transfer range, only if that outpost has a habitat and solar utility. The construction consumes **two previously manufactured modules (4 tonnes)**. Repeated or unauthorized construction is rejected.
2. In each complete 20-tick needs interval, an occupied, powered reclaimer consumes 1 additional game-energy unit per tick (when occupied). At most **10 kg wastewater and 10 kg CO₂ stream from each of the site's habitat and farm compartments** can be processed per period. There are no global inventories or teleported outputs.
3. Game mass-flow recipes: **10 kg wastewater → 9 kg reclaimed clean water + 1 kg sludge**; **10 kg CO₂ → 7 kg oxygen + 3 kg carbon-rich residue**. Each product returns to the same local habitat/farm compartment; byproducts remain with the facility. These are balancing approximations, not laboratory chemistry or proof of indefinitely closed circulation.
4. `ReclaimerFacility` persists installed mass, losses, lifetime production, power service history, and three-slot rolling snapshots of **recovered water, regenerated oxygen, water demand, oxygen demand**. A new `assess_life_recovery` view assesses **all currently occupied sites** independently, with power continuity and three interval measurements. It cannot claim coverage if one occupied site is missing equipment or if any of the three recent periods falls below demand. The `independent_renewal_proven` flag is always false pending external replenishment and full input/output validation.
5. Existing `RESILIENCE` frame layout and `WaterRenewalUnverified` blocker remain intact, preserving prior clients' field meanings. New `RECLAIMER` and `LIFE_RECOVERY` frames allow the browser to display throughput and residue. The local relay exposes exactly one new command: `colonyReclaimerBuild` with a bounded integer outpost and an authenticated local token/origin. No admin or autonomous-growth command is exposed.
6. A **v17** snapshot adds one strictly decoded `reclaimer` record per installed site. v1–v16 remain readable. New action enum appended (does not renumber existing actions). The `resource_ledger` adds `reclaimer_construction_t`, verified against the independent operation journal and execution event with exact endpoints, tick, material amount, and action type.

## Limitations / correct interpretation

- **Measured recycling is not resource renewal.** Reclaimed water/oxygen derives from existing local waste and CO₂; mass is lost to physical residue. Even apparent 100% short-window demand coverage cannot prove indefinite operation, feedstock availability, or no future imports.
- The earliest B7.0d–e farm recycling remains its own mechanism; this milestone adds site habitat recycling and bounded parallel processing. Old worlds without this new plant retain their prior behavior.
- No stellar illumination/shadow, water mining at the site, purification filter logistics, pressure/humidity systems, biological oxygen/carbon balances, detailed chemistry, medical specialization, true individual NPC needs, fully autonomous procurement, server-offline simulation, player inactivity penalty, or autonomous governance is implemented here.
- The simulated site throughput is capped at 20 kg of wastewater and 20 kg CO₂ total per site per needs cycle (10 kg per compartment), with solar demand now including the plant. Multiple populated sites must have their own plants.
- For existing worlds, snapshot v17 is reached only when a v17 action is accepted; old v16 snapshots are not rewritten merely because the new binary opens them.
- The regression includes fixture-only gate and older travel systems. They do not redefine the Single Galaxy starter world, which has one ship and no prebuilt colony infrastructure.

## Linux test evidence

| Verification | Evidence |
|---|---|
| New native reclaimer test | Passed: construction prerequisites, authorization, finite inputs, conservation, oxygen/water history, shortage/readiness assertions, power outages, v17 roundtrip, forged snapshot rejection, SQLite restart, recovered copy and tampered construction-ledger rejection |
| New live reclaimer/relay integration | Passed: real C++ host, SQLite restart telemetry, accepted token-authenticated command, malformed/forged/remote-origin rejection, served UI assets |
| New JavaScript parser/control tests | **3/3 passed**, including impossible mass-flow and false-independent-renewal rejection |
| All JavaScript regression tests | **78/78 passed** |
| Native regression tests | **49/49 have completed passing runs**: tests 1–12 passed; 13–24 included one transient B6.4 live multi-hop failure under parallel execution, which passed on isolated rerun; 25–36 passed; 37–49 passed. **Do not present as one interruption-free 49-test pass.** A preliminary serial group stalled at the older B6.5 QA live test and was not counted. |
| Linux production-style build | Passed with `-DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF` |
| Archive clean extraction verification | See `docs/B7_0k2_clean_*` logs in delivery |

## Next B7.0k or B7.0l subgate

1. Add **real local water supply and loss replacement** by extraction/mineral refining, sealed-tank logistics, industrial purification and powered piping without teleporting cargo.
2. Add oxygen process chemistry / carbon-recovery or electrolysis balance with explicit water / energy / hydrogen inputs and outputs; represent atmospheric storage and leakage.
3. Verify net local production/demand over much longer windows, including imported supplies, emergency buffer thresholds, deterioration, shutdown and recovery.
4. Only after those chains exist, review the `WaterRenewalUnverified` blocker. **Do not enable autonomous growth** until healthcare, rest, quality of life, institutional governance and managed absence are genuinely supported.

## Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R 'galaxy_b70k_(reclaimer_tests|live_reclaimer_relay)' --output-on-failure
node --test tests/B70kReclaimerNodeTests.js
node --test tests/*.js
```
