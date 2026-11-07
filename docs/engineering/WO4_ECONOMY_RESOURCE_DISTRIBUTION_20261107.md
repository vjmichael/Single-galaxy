# WO4 — Economy, consumption, resource allocation and distribution
**Started 2026-11-07** on `industry/wo4-resource-demand-20261107`, stacked on **WO3 draft PR #89** (itself stacked on WO2 #88 and WO1 #87).

## Binding owner decisions
- No mandatory money, wages, chits, player trading market, PLEX or fictional wealth is required to allocate resources. Resource access, production, scarcity, real stores and sustained NPC service are the economy.
- All people are original named `NpcRecord`, original `PhysicalDependent` or authorized macro cohorts. All inventories and production come from the original `Simulation` / `PersistentWorld` and existing physically built installations. No second storage or workforce truth.
- Neither demand nor an allocation forecast is permission to transfer. Real movement requires the original owner-authorized site/shuttle/transport orders and their SQLite receipts; WO5 is the concentrated physical freight-integration stage.
- The existing `Industry01Catalog` entries describe potential needs and recipes. Only native executable, installed and physically stocked facilities may actually consume/produce; catalogue membership by itself is not a player asset.
- Distinguish orbital habitat/farm, planet surface, source ship, warehouse and regional cold-mode. Do not add inventories across compartments as though they were already hauled together.

## WO4 development plan

| Work item | Scope and exit condition | Status |
| --- | --- | --- |
| **WO4-A** Demand and physical stockpile visibility | Read exact physical stock and current users by owner and location; report bounded reserve need/pressure with stale snapshot detection; no new authorizations or inventory | **SOURCE IMPLEMENTED; native unverified** |
| **WO4-B** Prioritized allocation policy | Explicit owner policy for essential life support versus work, maintenance and discretionary goods; deterministic scarcity choices that never promise more than available and cannot self-approve | **OPEN** |
| **WO4-C** Owner-approved local allocation | Hook approved plans to *existing* actual-world commands for intra-site/depot custody; atomic quantity reconciliation, denial and rollback without inventing cargo | **OPEN** |
| **WO4-D** Item/service consumption profiles | Link native recipe/material catalogue to consumables, durable assets, wear/repair and actual NPC use; no unimplemented food/medical/service fabrication | **OPEN** |
| **WO4-E** Region-scale demand and distribution requests | Aggregate separately identified supply/demand under correct cold/detailed ownership, create bounded shipment requests only, preserve original dispatch and WO5 transport authority | **OPEN** |
| **WO4-F** Native verified acceptance | Exact-head Windows/Linux CMake + CTest, real original stores, faulty approval/replay/overflows, actual independent SQLite copy, WO2/WO3 regression | **OPEN** |

## WO4-A implementation (source committed, no native acceptance yet)

- New `include/galaxy/WO4ResourceDemand.hpp` provides `economy04::observe_owner_world` using **only** the original ship `ColonyLifeSupport`, constructed outpost habitats/farms and `colony03::audit_planetary_surface` for physically present planetary consumers. Each compartment keeps its own physically present kg. Foreign-owner and missing/invalid physical source are rejected.
- For food, water and oxygen, `economy04::calculate` reports current kg, one-window need, configurable bounded four-window default reserve target, deficit, buffered kg above target (explicitly **not** free stock or an authorized transfer), complete windows and pressure: empty/critical/needs-restock/meets reserve. Overflow, invalid ration and negative stock fail closed, not silently clamped.
- A planning *window* is explicitly **not an actual tick, production rate or consumption-cycle guarantee**. This initial demand forecast does not simulate or schedule demand, withdraw goods, run NPCs, or provide a 20-tick horizon. That requires subsystem-specific measured consumption profiles in WO4-B/D.
- `Snapshot` records the current original tick and state hash, so any intervening real movement/consumption/production/owner change makes prior observations stale. It stores no shadow stock and writes no commands.
- `tests/WO4ResourceDemandTests.cpp` now exercises pure shortage/wear-pressure bounds, foreign-owner and bad-source refusals; constructs an original outpost/habitat from real mined/refined/fabricated modules, actually transfers finite ship provisions and reassigns original named residents, checks original conservation and no changes from mere observation, and compares a separately copied SQLite restored world forecast. Source test **has not executed natively**.
- The actual WO3 planetary utility regression also asserts the projection of a genuinely physically built and provisioned surface site, uses its existing citizen/visitor/dependent count, and proves a read-only state hash.
- `CMakeLists.txt` registers `galaxy_wo4_resource_demand`; exact-head Windows/Linux GitHub workflow `.github/workflows/wo4-resource-demand.yml` builds WO4 + original WO2/WO3, runs targeted CTest and preserves JUnit/CTest logs for executed jobs. Previous WO3 CI had runner-zero failures; neither this code nor WO3 should be declared accepted without actual native logs.

## Caveats
- The reserve assessment is **observation**, not `PersistentWorld` commitment, escrow, actual rationing policy or staff action. No NPC may autonomously move stock based on these values.
- The currently observed virtual time is the authoritative `world.tick()`, but reserve windows have no enforced correspondence to the different actual ship/habitat/planet/cold regional life-support consumption cadences.
- The physical site `colony03` census is reused without inventing a new resident count; any underlying WO3 duplicate-count issue must be fixed at the source census instead of compensated in WO4.
- Explicitly defer all installed Windows 3D game acceptance until usable original assets per owner's prior instruction.

## Next task
**WO4-B: owner-approved priority policy and deterministic finite-stock allocation preflight.** Implementation must express hard vital minimums, local material scarcity and reservations as *proposals* until a trusted original-world writer records real allocation. Keep WO3-F independently open for native CI.
