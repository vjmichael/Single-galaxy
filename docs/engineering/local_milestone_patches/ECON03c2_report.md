# ECON-03c2 — Cold Colony Event Batching
**Date:** 2026-11-01  
**Exact baseline:** `SingleGalaxy_ECON03c1_Macro_NPC_Needs_2026-11-01_Source_Tests.zip`  
**Source:** `SingleGalaxy_ECON03c2_Cold_Colony_Event_Batching_2026-11-01_Source_Tests.zip`  
**Patch:** `SingleGalaxy_ECON03c2_Cold_Colony_Event_Batching_2026-11-01_Changes.diff`

## Outcome
A tested, separate cold-population execution kernel can now advance very large
cohorts through long virtual time intervals without one NPC/tick iteration. It
uses the real `MacroNeeds` consumption routine, checks for material shortages,
handles same-tick scheduled deliveries and population changes, and consumes only
stock physically present in each compartment. Delivery moves real inventory
from the cohort's separate depot; it cannot conjure material. Whole-advance
rollback prevents partial mutations on invalid/overflowing events.

## Verification
- Linux CMake/Ninja Release compiled the shared `galaxy_kernel` and three tests.
- Targeted CTest: **3/3** (`galaxy_econ03b_market_recovery_tests`,
  `galaxy_econ03c1_macro_needs_tests`, `galaxy_econ03c2_cold_colony_tests`).
- New independent 100-tick reference matches a batched 100-tick interval.
- Split/restart-equivalence, exact transfer conservation, same-tick deadline,
  shortage counter, overflow and invalid-event rollback covered.
- A 1.3-trillion-person cohort and 1-trillion-tick interval are covered in one test.
- Exact patch replay reproduced all **656 source files**.

## Important limits
This is **not** a connected, production galaxy LOD scheduler. The live
`Simulation::step()` still processes all current active systems. Remote factories,
transport, power/production, population migration and attacks need their own
verified event boundaries before whole regions can be advanced in coarse steps.
No new SQLite persistence format, full native suite, Windows acceptance or
consumer-hardware benchmark has been claimed. GitHub remains unchanged.

## Next gate
Add a durable per-region event queue and deterministic checkpoint/replay for
production, arrivals, energy, needs and construction. Promote a region to
detailed simulation as players approach; synchronize authoritative state.