# ECON-03c1 — Macro NPC Consumption and Resource Runway
**Date:** 2026-11-01  
**Exact baseline:** `SingleGalaxy_ECON03b_Resource_Markets_Recovery_2026-11-01_Source_Tests.zip`  
**Source:** `SingleGalaxy_ECON03c1_Macro_NPC_Needs_2026-11-01_Source_Tests.zip`  
**Patch:** `SingleGalaxy_ECON03c1_Macro_NPC_Needs_2026-11-01_Changes.diff`

## Engineering result

A deterministic population-needs macro kernel (`MacroNeeds`) now processes one
physical compartment and a population count per consumption event. It calculates
elapsed consumption windows algebraically instead of running cooldown timers
for each NPC. Existing ship, habitat and farm consumption paths use it without
changing the established 20-tick rate, mass conservation or the save format.

A no-replenishment runway forecast is exposed in `ResourceMarket::SiteView` and
local host `RESOURCE_RUNWAY` telemetry. It reports the next needs deadline,
remaining food/water/oxygen ration periods and first potential shortfall.
Warehouse quantities remain separate: no inventory teleportation or guaranteed
access is implied by the market listing. The market's internal quantity cap is
replaced with checked int64 arithmetic, with reserve multiplication guards.

## Verified locally

- Linux CMake/Ninja Release builds: relevant current kernel/host and test targets.
- 8 distinct native tests passed, including existing B7.0d/f/g/k/l/n and
  ECON-03b plus new ECON-03c1.
- 3 live host/relay tests passed (habitation, utilities, reclamation).
- 93/93 JavaScript tests passed.
- New test includes a 1.3-trillion NPC macro cohort, four elapsed windows,
  5.2-trillion-kg per-resource ledger deduction, recovered waste conservation,
  partial shortages, arithmetic overflow refusal, restart and SQLite recovery.
- Exact patch replay verified all 651 source files from 646-file baseline.

## What is NOT complete

This validates the **algorithm's O(1) headcount behavior**, not a working
trillion-NPC galaxy. The active Simulation still ticks physical ships and
facilities at active-world cadence; cold-region event queues, true tiered
population simulation, wider persistence/storage range limits, additional
household resource categories, and complete shipping contracts require future
work. No full native suite, Windows graphical test, MMO server or automatic
resource transfers are claimed. The 20-tick ration is a gameplay placeholder.
GitHub remains unchanged.

## Next gate

Use food/water/oxygen shortfall deadlines to prioritize host-authorized
resource reservations and NPC hauls between real storages and consumers,
with ledgered receipts and rollback of canceled deliveries.

## Changed source paths

- `CMakeLists.txt`
- `docs/design/ECON03c1_MACRO_NPC_CONSUMPTION_2026-11-01.md`
- `docs/engineering/ECON03c1_MACRO_NEEDS_2026-11-01.md`
- `include/galaxy/MacroNeeds.hpp`
- `include/galaxy/ResourceMarket.hpp`
- `src/MacroNeeds.cpp`
- `src/ResourceMarket.cpp`
- `src/Simulation.cpp`
- `src/colony_live_host.cpp`
- `tests/ECON03bMarketRecoveryTests.cpp`
- `tests/ECON03c1MacroNeedsTests.cpp`