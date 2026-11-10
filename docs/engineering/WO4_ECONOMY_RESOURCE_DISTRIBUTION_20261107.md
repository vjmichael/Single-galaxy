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
| **WO4-B** Prioritized allocation policy | Actual vital reserve protection; current-window before reserve; bounded deterministic offers, policy consent, adversarial forged-snapshot refusal | **SOURCE IMPLEMENTED; native unverified** |
| **WO4-C** Owner-approved local allocation | Original ship→built habitat/farm vital commands and original silicate→aluminum→constructed-storage material transfers; host attestation, original receipt and rollback | **SOURCE IMPLEMENTED in supported native paths; native unverified** |
| **WO4-D** Item/service consumption profiles | Original 10/20-tick life-support cadence profiles; real aboard people/dependents; INDUSTRY-01 feedstock, durable, consumable and service requirements; no invented generic wear | **SOURCE IMPLEMENTED for proven native paths; wider service/wear execution deferred** |
| **WO4-E** Region-scale demand and distribution requests | Owner-scoped physically separate stocks; bounded cross-ship/planetary vital requests that never book or move freight; real carriers deferred to WO5 | **SOURCE IMPLEMENTED; actual transport intentionally deferred** |
| **WO4-F** Native verified acceptance | Original Windows/Linux CMake/CTest, real materials, owner authorization, stale/forged grants, SQLite failpoint/rollback/restart, WO2/WO3 regressions | **OPEN — runners/tests not yet executed** |

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

## Remaining work after the WO4-B–E source pass
**WO4-F real exact-head native acceptance remains open.** No GitHub hosted Linux/Windows test completion or installed-game source verification can be asserted from a queued workflow. Repair any real compiler, CTest, SQLite or receipt failures only when actual logs exist. Material destinations beyond the four native constructed storage items, clinic/utility wear outside the native implemented commands, and flight-carrier execution belong to later qualified integration—not unverified simulated work. WO3-F stays separately OPEN.

## WO4-A planetary consumer census correction — source-only checkpoint

During direct integration review, the native `colony03::audit_planetary_surface` census was found to count an original registered child **twice** when its three-seat family shuttle remained landed and the real family had completed legal planetary admission: once in the bound family shuttle manifest and again in the admitted household loop. The child remained one original `PhysicalDependent` identity; the duplicated count was an observational bug, not an actual new person.

- WO4 branch now counts a physically present admitted household child separately **only if not already counted** in the landed original family manifest. It does not change custody, population or supplies.
- The genuine v118 ART02 admission regression checks the count against the actual two existing planetary workers plus separately identified original disembarked visitors, shuttle pilot, boarded parents and one original child; WO4 scarcity projection must agree.
- Source commits `07a5cb87` and `42e68232`. This is a **WO3-audit correction on the stacked WO4 branch**, not yet propagated to WO3 PR #89. Do not claim WO3-F native acceptance from this amendment. Before WO4 is merged, reconcile the census correction into the WO3 base to avoid divergent source behavior.
- Source asserts are committed but still unexecuted natively; require exact-head Windows/Linux CMake/CTest.


## EvEmu 66 — WO4-B/C/D/E/F implementation checkpoint (2026-11-07)

**Current source changes live on stacked draft PR #91; none is a native acceptance result.**

### WO4-B — owner-directed finite allocation
- `WO4Allocation.hpp` derives deterministic **critical-current-window then reserve replenishment** offers from the original `Snapshot`; ship provisions are protected at their own owner-selected reserve floor. Stocks never move just because a planner proposes them.
- `owner_enabled` must be explicitly set to issue proposals. The system refuses stale world hashes, mismatched policy, unbuilt outpost destinations, impossible source stock and **fake resource snapshots whose hash was copied from the real world but whose demand or reserves were altered**. An offer is not a stock reservation; host must reobserve after the first native change.
- Priorities distinguish vital now, vital reserve, maintenance, production and discretion. Mandatory vital protection is never silently overridden by a proposed industry program.

### WO4-C — actual supported original-world allocation
- `WO4LocalAllocation.hpp`: trusted authenticated-host approval sends only the original `Action::TransferHabitatStock` / `TransferFarmStock`, with original origin, source owner, destination, positive quantity, native ingredient selector and unique command ID. The existing SQLite-backed `PersistentWorld::submit` plus next actual `step` perform the movement. Native `Event.result` and `Event.transferred` report success; rejected, stale, duplicate and spoofed offers never submit.
- `WO4MaterialAllocation.hpp`: original manufactured `good/aluminum`, panel, frame and scrap goods may be offered from the actual ship hold into an owner-built original `OutpostStorage` via the **existing** `Action::TransferStorageStack` (ship→storage selector); original `Catalogue::standard()`, real free liters, existing stored stock and protected ship minimum govern quantity. Other fake/unbuilt material families refuse; this does **not** create a separate factory or unlock nonnative recipes.
- Both native operations are separately durable underlying original command submission and native execution, not a novel single multi-command transaction. Original SQLite failpoints preserve pending-command recovery; neither planner stores separate material, promises instant dispatch or bypasses the physical site/owner checks.

### WO4-D — physically grounded needs and item/service roles
- `WO4ConsumptionProfiles.hpp`: original ship/habitat/farm 20-tick per-person vital periods, and explicitly conditional planetary 10-tick support for 2 stationed workers + 20-tick shuttle visitor/registered dependent needs. These are read-only native cadence observations; actual production, shortages, recovery and native consumed amounts remain exclusively in the original world.
- `WO4ResourceDemand.hpp` now reads actual physically aboard named adults and original dependents when v57 passenger needs apply, not a stale aggregate crew count.
- **Native physical bug repair on the WO4 branch:** `Simulation::process_planetary_surface_operations` no longer debits a legally admitted child twice as both a shuttle manifest passenger and a household dependent; it also does not charge a shuttle child as present while the vehicle is not landed. The already repaired `colony03` audit counts the same unique physical people. This correction is on WO4's stacked branch and must be reconciled into WO3 base before final integration.
- The source-generated INDUSTRY-01 catalogue is read **as data**: item roles feedstock, intermediate, durable, consumable, waste; recipes' native binding; service consumable/equipment lists. No clinic episode, laundry service, entertainment performance or durable-chair wear is faked as having occurred just because an item definition exists.

### WO4-E — physical regional demand, not magic shipment
- `WO4RegionalDistribution.hpp` aggregates independently stored physical needs/surplus per original owner-controlled colony ship, each built outpost compartment and planetary site. It creates **unbooked**, bounded source→destination requests, first for one-window urgent survival then for reserves; available source buffer cannot be offered twice.
- Same-ship ordinary local deliveries handled under WO4-C are not published as second regional shipments. No regional owner transition, carrier identity, physical route, approval, escrow, receipt or actual freight transfer is invented. Original `RegionalHostCommands` and WO5 freight interfaces remain the sole qualified path to physical shipping.

### WO4-F — source verification implemented; native acceptance OPEN
- `tests/WO4ResourceDemandTests.cpp`: original finite construction, real habitat stocks and named residents; owner/forged/stale/duplicate rejection, native transfer receipt, conservative priority, no material creation, catalogue/service taxonomy, real mixed-compartment regional board and independent copied-SQLite parity.
- `tests/WO4MaterialAllocationTests.cpp`: real physical silicate mining/refining into original aluminum goods, installed storage, trusted native transfer into the actual 4m³ bin, original stock/material conservation, denial/tamper/idempotency and independent copied-SQLite restart.
- `tests/WO3PlanetaryUtilityTests.cpp`: original ART02 family admission now verifies unique surface-need counts, live WO4 forecast and separate native 20-tick consumption bounds.
- Fault-injected `FailPoint::AfterReceiptWrite` on the original WO4-C vital command tests rollback of its SQLite execution step while the original submitted ID remains pending; original retry applies ONCE and independent copied SQLite recovers the exact physical result.
- `CMakeLists.txt` registers both `galaxy_wo4_resource_demand` and `galaxy_wo4_material_allocation`. `.github/workflows/wo4-resource-demand.yml` builds both plus upstream WO2/WO3 tests under exact PR head on Windows/Linux, emits JUnit and retains CTest logs when jobs genuinely execute.
- Latest observed WO4 workflow `42402175250` at source commit `3de0b2fe` was **QUEUED**. That is not proof of OS native tests. No matching current local clone of private repository was available in the isolated Debian container, even though CMake/g++ are installed.
- **Acceptance gate:** obtain exact-head real Linux+Windows configure/build/CTest output, repair any compiler/runtime/SQLite failures, then mark WO4-F native complete. Installed Windows 3D gameplay remains deferred until true assets as separately directed by owner.

**Next original stage after WO4 source implementation:** WO5, transport/logistics integration. This is a later task and is not started here.


## WO4 material and maintenance scope expansion — later source checkpoint (2026-11-07)

- **WO4-E manufactured goods:** `include/galaxy/WO4GoodsRegional.hpp` now plans owner-declared regional requests for the FOUR actually supported original material goods (aluminum, panels, frames, scrap). A requested delivery has an existing owner-controlled ship or actual built outpost as destination; source supplies come only from the original cargo snapshot of existing owner ships. The planner subtracts owner-declared ship-retained stock, honors maintenance before production before discretion, never double-offers the same material, reports uncovered demand and leaves every carrier/route unbooked. Same ship→its own storage does not also appear as cross-region freight. All other unknown or future goods remain absent, not teleported by a generic industry catalogue.
- **WO4-E native fixture:** The material regression now also has a second original owner ship with real 100 kg+ genesis life-support stock. It verifies explicit owner-approved cross-ship material request: 80 kg maintenance first, 20 kg production, remaining 30 kg unserved, no physical movement and no forged local-shipment duplication.
- **WO4-D actual installed utility:** `observe_native_utility_maintenance` reads the original physically built utility's condition, spare modules, technicians and powered state with exact original owner checks. It does not invent physical wear, generate parts or schedule repair. The native material fixture physically constructs a utility and tests the observed original condition/spares and conservation after installation.
- **Native gate remains unchanged:** tests and workflow source have been committed, but neither OS build nor CTest is accepted without a real assigned runner and executed logs. Broader nonnative goods/institutions, item-specific wear algorithms and actual intercolony freight are explicitly outside source-completed WO4 and require separate verified native capabilities, particularly WO5.

## WO4-E regional authority CAS guard — source-only checkpoint

`WO4RegionalDistribution.hpp` now distinguishes a **plain read-only, unverified regional scarcity view** (its default) from `observe_detailed_regional_distribution`, which reads the real persisted `RegionalModeAuthority` before AND after the snapshot, requires stable **Detailed** ownership, stable phase, caller-provided expected nonzero generation/checkpoint revision and a nonfuture ownership boundary. It fails closed for Cold/pending or mismatched generations. Even the guarded view is never a shipping authorization: the host must bind the correct region scope to the world, and later original `RegionalHostCommands`/WO5 must independently verify the actual physical carrier and commit generation. The C++ regression verifies unguarded views cannot report a verified owner generation.

No native Windows/Linux result has been received on this exact PR head. WO4-F remains open.
