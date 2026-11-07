# WO3 — Planetary colonies and their operation
**Development opened 2026-11-06.** Owner authorized progression to WO3 while WO2-J Windows/Linux native acceptance remains open. Branch `industry/wo3-colony-operations-20261106` is **stacked on** WO2 draft PR #88, not an independent release or merged code.

## Authoritative boundary
The original `Simulation`, `PersistentWorld`, per-owner `Entity`, canonical `NpcRecord`, original `built_outposts`, `planetary_sites`, physically stored life-support resources, and original accepted commands remain the only sources of physical truth. WO1 recipes define **potential goods**, WO2 professions define **potential jobs**; neither magically creates an outpost, planetary house, available worker, utility, imported water or operational restaurant.

**Two different settlement types already exist:**
- Original player-built **orbital/outpost station** (`BuiltOutpost` + physical `HabitatFacility`, `FarmFacility`, `UtilityFacility`, `ClinicFacility`, `CommunityFacility`, `ReclaimerFacility`, `WaterworksFacility`, `OutpostWorkshop`, `OutpostStorage`). Its facilities use original construction commands and local ship-derived modules.
- Original **planetary surface site** (`PlanetarySiteFacility`, `PlanetarySiteKey`, original finite delegated NPCs, surface production and shuttles). It is **not** an outpost station and has **no proven permanent planetary housing, full industry institutions or sustainable colony life support** merely because a spaceport exists. Ship-to-surface movements must remain physically executed by the original shuttle/cargo system.

No currencies, wages, free seeded NPCs, independent second colony store, automatically built NPC infrastructure, new gameplay client, 3D art, or teleportation. Preserve original custody, migration, terrain, body-specific geology, owner authority, and actual supply constraints.

## Work order breakdown and gates

| Work item | Required actual result | Current state |
| --- | --- | --- |
| **WO3-A — Native site capability audit and owner construction preflight** | Source of truth census and utility/food/water/oxygen view; enumerate **only original buildable** facilities, exact modules and prerequisites; proposal requires explicit owner approval and executes through original C++ commands and SQLite, never a separate new construction executor. | **Source implemented**. `include/galaxy/WO3ColonyOperations.hpp`, `tests/WO3ColonyOperationsTests.cpp`, CMake and exact-head Win/Linux CI workflow. Native tests NOT yet accepted. |
| **WO3-B — Permanent planetary habitat and physically supported sites** | Add actual surface housing/installed structural equipment with real item/module construction receipts, original NPC build labor and commissioning, finite capacity, water/food/oxygen support and terrain/siting guards; do not conflate outposts and planetary sites. | **Open** |
| **WO3-C — Utilities and ongoing provision/consumption** | Bounded local water/power/waste/HVAC, nutrition, needs cycles, loss-bearing recovery, no stock teleportation; distinguish sustainable output from depleted stores. | **Open** (existing code is a foundation) |
| **WO3-D — Player-authorized NPC construction and worksite organization** | Physical site projects use WO2 named trade qualifications where native institutions exist, original material input and labor time, owner approval, durable suspension/repair, authorized site managers. | **Open** |
| **WO3-E — Census, residency, migration and departure** | One canonical resident/visitor/temporary worker distinction, physical shuttle/transport custody, family/relocation policy, housing limits and outcomes; no population injection. | **Open** |
| **WO3-F — Operations/recovery and native acceptance** | Fail-safe shortages, maintenance, restart/independent SQLite recover-copy, simultaneous owner conflicts, hostile site claims; Linux+Windows original native CTest and inherited colony/industry/WO2 regressions on exact head. | **Open** |

## WO3-A implementation
`colony03::plan_facility` determines owner/site authority from actual original player ship, `BuiltOutpost`, `Entity` ownership, physical 2.5 km station distance and committed finite ship modules/crew. It enumerates the exact ten supported buildable cases: farm, habitat, utility, community, clinic, reclaimer, waterworks, workshop, frame finisher and one original storage. It checks their **real** prerequisite installed facilities, including the appropriate habitat, utility and workshop. A forecast is not a reservation.

`colony03::approved_original_facility` refuses absent approval, stale/accepted IDs, foreign owners and blocked construction. When allowed, it only produces an authentic `Action::Construct*` original `Command` with the owner/ship/site and next original tick; `PersistentWorld::submit` plus the original authoritative effect may still reject changed conditions. No new construction action or storage layer is introduced.

`colony03::audit_original_outpost` reports real housing, separate physical habitat provisions, installed utilities, module/crew reserves and direct named NPCs physically present versus their underlying original assigned role. It does **not** infer long-term renewable supply or autonomous population growth. It exposes exact aggregate/headcount reconciliation flags to detect mismatch instead of quietly synthesizing NPCs.

`colony03::audit_planetary_surface` reports original world-owned planetary site/spaceport/policy and finite stock and cross-checks original `delegated_npcs` against real `NpcRecord::location` and `NpcRole::PlanetarySurface`. It does **not** pretend the orbital station facilities live on the planet, that temporary workers are permanent settlers, or that a shuttle implies planetary housing.

The WO3 native CTest uses real `Action::Mine` → `RefineOre` → `FabricateModule` → `ConstructOutpost`, then actual issued original facility commands. It checks ten facility admission gates, building consumption versus surviving mass, explicit approval, immutable IDs, owner denial, original named resident counts, pending-command independent SQLite `recover_copy`, post-execution replay and exact original-world state hash.

**Evidence status: source+test/workflow committed, actual Windows/Linux compiler/CTest/SQLite results pending.** No Linux/Windows native test or installed-game success can be claimed until it really runs. Workflow: `.github/workflows/wo3-colony-operations.yml`. Keep WO3 draft and stacked on WO2; WO2-J native acceptance remains independently open.

## Next implementation
Start WO3-B with **actual physical planetary** habitat and capacity/siting rules reusing the existing `planetary_sites` original-world snapshot, resource inventory, shuttle, owner checks and named-NPC work. First design a migration-safe record/schema and physical bill of materials, then implement authorized slow build, provision, resumption and failed-copy/tamper gates. Do not grant 29 new industry factories merely because WO1/WO2 have recipe and role profiles.
