# DEMOG-03 — Native NPC pioneer expansion advisory planner

**Date:** November 1, 2026
**Status:** C++17 local source and focused tests completed. **Non-authoritative advisory only**, not live NPC colonization.

## Design basis
NPC-founded daughter colonies should be emergent from forecast housing demand, food/water/oxygen shortages, mineral scarcity, and attractive surveyed settlement sites. Expansion must conserve people and resources and respect the player/founder's delegated authority. Use a small founding ark; the current **trial** is 500 colonists, 40 active gameplay-hour average NPC lifespan, 3–5 children per participating couple (4 as testing midpoint), 80–150-person expeditions, and 400–800-person **soft** daughter settlement planning sizes. No hard city/factory population caps.

See [design decision](../design/NPC_PIONEER_DIASPORA_COLONY_EXPANSION_2026-11-01.md) and [DEMOG-02](DEMOG02_SMALL_SETTLEMENT_STARTER_SWEEP_2026-11-01.md).

## Actual local engineering completed
Against the earlier local ECON-03c10 native source package:
- New `include/galaxy/PioneerExpansionPlanner.hpp` and `src/PioneerExpansionPlanner.cpp` define the pure, deterministic advice engine, pressure reasons, configuration policy, candidate site validation, supply/ship safety gates, and manifest requirements.
- New `tests/DEMOG03PioneerPlannerTests.cpp` exercises 500-person proposal, missing ship/supplies/adults, surveyed and claimed sites, dangerous sites, duplicate IDs, low food and declining-population evacuation, soft settlement goals, deterministic selection, integer overflow, and **5,000 repeated proposals**.
- `CMakeLists.txt` registers the planner with `galaxy_kernel` and the CTest target.
- Existing native `PopulationCohorts` is used in the test to confirm zero side effects from planner advice and conservation under a separately executed transfer.

## Evidence
- **3/3 local targeted CTests PASS**: DEMOG-03 planner, kernel simulation, persistence.
- Focused native AddressSanitizer + UndefinedBehaviorSanitizer execution **PASS** on final source.
- Patch **dry run PASS** against the original ECON-03c10 source archive.
- The exact build patch, sources, test harness, logs and technical report are delivered in the conversation artifact `SingleGalaxy_DEMOG03_Native_NPC_Expansion_Planner_2026-11-01.zip`.
- This commit is the engineering record, **not a claim that patch source is merged to the current remote main or that all original game tests pass**.

## 500-person acceptance example
Source 500 residents, 240 adults, forecast 650 against 650 housing, existing capacity-120 vessel, surveyed safe vacant destination, sufficient available physical stocks. Planner proposes 100 settlers (at least 70 adults), plus 200 units each food/water/oxygen and 650 material units. **No actual inventory or population is altered**, no ship is created and the proposal always requires host authorization.

## Next gate: DEMOG-04
Authoritative durable expedition receipts and life cycle:
1. Validate host permission and delegated resource spending against actual `PersistentWorld`.
2. Reserve real ship, exact people (including family/occupation records), cargo and construction materials in one transaction.
3. Journal departure, transit and destination reservation without cloning or losing people.
4. Advance route, risk, resource consumption and mortality once; count in-transit travelers as population.
5. Host-authorized arrival builds only prepaid starter infrastructure, ties destination to native colony/world records and commits exactly once.
6. Validate save/restart/replay, cancellation, loss/destruction, cold/warm ownership and recovery before enabling NPC auto-launch.
7. Connect actual resource production and demographic aging/births to a shared authoritative clock.

The C++ planner is **not** a substitute for the real economy and population ledgers. No native gameplay change is activated by this documentation.
