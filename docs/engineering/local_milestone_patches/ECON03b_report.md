# ECON-03b — Resource markets and reclamation engineering report
**Date:** 2026-11-01  
**Exact baseline:** `SingleGalaxy_ECON03a_Resource_Only_Host_Source_Tests_2026-11-01.zip`  
**Result:** `SingleGalaxy_ECON03b_Resource_Markets_Recovery_2026-11-01_Source_Tests.zip`  
**Patch:** `SingleGalaxy_ECON03b_Resource_Markets_Recovery_2026-11-01_Changes.diff`

## What changed

- Introduced `ResourceMarket` as a native, read-only survey of actual constructed outposts. It tracks colony stock, known resident food/water/oxygen demand, reserve targets, shortages, potential supply and per-site waste streams.
- Supports four levels of **market discovery**: site-local, same star system, same region, inter-region. Source/destination opportunities are indicative only. No currency, wages, premium tokens, global player market or automatic goods transfer exists.
- Site inputs are validated against actual built-outpost records and authoritative system IDs; region identifiers are supplied by the trusted host. Summaries do not add new stock, and scanning does not write to the simulation or SQLite.
- Reclaimer plants, when constructed and powered, process source-separated residential organic waste at a game-balanced rate of **10 kg → 8 kg compost + 2 kg residual sludge**, alongside existing 10 kg wastewater → 9 kg recovered water + 1 kg sludge and the existing CO2 process. Outputs stay local. Medical waste is not composted. Mass is conserved and existing save fields are reused.
- The existing local colony host now emits bounded, read-only `RESOURCE_MARKET` telemetry every 20 ticks, for food, water, oxygen and compost at built outposts.
- Updated the design contract to restore resource-only local/system/regional/inter-regional markets while retaining solo-first and invite-only multiplayer. Added explicit waste-recovery choices and limitations.

## Verification

- **Linux native build:** `galaxy_kernel`, `galaxy_colony_live_host`, `galaxy_econ03b_market_recovery_tests`, `galaxy_b70k_reclaimer_tests` compile with CMake/Ninja Release.
- **CTest 3/3:** `galaxy_b70k_reclaimer_tests`, `galaxy_b70k_live_reclaimer_relay`, `galaxy_econ03b_market_recovery_tests` (including actual SQLite restart and mass-conservation checks).
- **JavaScript 93/93:** `node --test tests/*.js`.
- **Exact-source verification:** 641 baseline files; 646 resulting files; 10 changed/added/deleted paths; `patch -p1` replay against the exact ECON-03a source reproduces all 646 resulting files byte-for-byte.
- No full native suite, Windows build, finished multiplayer, regional shipping execution, or graphical market panel is claimed for this increment.
- GitHub not modified; this was developed and tested locally.

## Important limitations

- Resource markets are not executed contracts: physical inventories do not move; available stock may be in separate compartments, so real pipes/haulers, reserve locks, permissions, and transport routes are future gates.
- The live host is currently local-system only, with region metadata placeholder. Cross-system/regional market reports are implemented in the generic survey, **not yet connected to live interstellar maps and logistics**.
- Currently first-class recurring market demand covers food, potable water and oxygen; future NPC household goods, healthcare, spares, fuel, steam/thermal demand, and trash collection remain to be modeled.
- Water and source-separated biological waste reclamation are operational; steam condensation and generic solid-trash sorting are proposed later, not silently simulated now.

## Immediate next gate

**ECON-03c:** signed, host-authorized resource offers and NPC transport tasks across existing warehouse/physical inventory endpoints; per-reservation stock custody; journaled pickup/delivery; aborted-haul cleanup; receipt-level conservation and restart tests. Extend demand modeling and player-visible market status only after correct resource movement.

## Changed paths

- `CMakeLists.txt`
- `docs/design/ECON03b_RESOURCE_MARKET_WASTE_RECOVERY_2026-11-01.md`
- `docs/design/RESOURCE_ECONOMY_INVITE_ONLY_AND_FUTURE_MMO_2026-11-01.md`
- `docs/engineering/ECON03b_RESOURCE_MARKET_RECYCLING_2026-11-01.md`
- `include/galaxy/ResourceMarket.hpp`
- `src/ResourceMarket.cpp`
- `src/Simulation.cpp`
- `src/colony_live_host.cpp`
- `tests/B70kLiveReclaimerTests.py`
- `tests/ECON03bMarketRecoveryTests.cpp`