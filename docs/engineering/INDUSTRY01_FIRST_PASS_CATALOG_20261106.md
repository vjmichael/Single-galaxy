# INDUSTRY-01 — First source-grounded industry graph and candidate recipe catalogue

> **LATEST IMPLEMENTATION:** This is the earlier 96-item/69-recipe snapshot. See the [full WO1 source implementation closeout](INDUSTRY01_WO1_SOURCE_CLOSEOUT_20261106.md) for the up-to-date 100-item, 71-standard-recipe, 3-lossy-recovery, 8-service, 28-native-identity registry and two connected real-workshop recipe bindings. Source integration is committed; exact-head native CI pending.
**2026-11-06 · first coding slice · NOT WORLD-AUTHORITATIVE**

**Development priority:** the owner's [20-stage sequence](PROJECT_DEVELOPMENT_SEQUENCE_20261106.md), with industry recipes first. Companion architecture and research brief: [industry supply-chain research](INDUSTRY_SUPPLY_CHAIN_RECIPE_RESEARCH_20261106.md). This stage does **not** resume general player gameplay, implement NPC work schedules, create a currency or add infrastructure at genesis.

## Committed first slice

- [`data/industry/industry01_candidate_catalog.json`](../../data/industry/industry01_candidate_catalog.json) — **96** candidate material/item classes, **69** source-labelled representative mass-balanced transformations, **8** staff/equipment-dependent services, **8** explicit old-world identity bridge entries.
- [`scripts/validate_industry01.js`](../../scripts/validate_industry01.js) — a **pure and fail-closed** graph validator for canonical IDs, duplicate codes, research status, provenance, grams/count units, positive cargo volume, integer time/power, exact game-material mass closure, acyclic straight-line manufacturing, service referential integrity, and unresolved legacy world conversions.
- [`tests/INDUSTRY01_CatalogTests.js`](../../tests/INDUSTRY01_CatalogTests.js) — 6 regression scenarios: accepted graph; shared PCB cross-sector reuse; malformed items/recipes/mass/sources; cycle injection; unsafe legacy conversion; invalid service capacity.
- Validation was exercised in the connected JavaScript V8 runtime against the actual committed source and JSON content, **six checks passed; native Node test execution and full Windows/Linux CI are NOT yet accepted**. Run locally: `node scripts/validate_industry01.js`; `node --test tests/INDUSTRY01_CatalogTests.js`.

## What the recipe graph *actually* covers

| Shared material source | Intermediates | Representative finished goods / use |
| --- | --- | --- |
| Aluminum ore, copper ore, iron ore | Refined aluminum/copper/iron, structural alloy, fabricated frames/panels, fasteners, wire | Ship construction components, chair/desk/table frames, generators, printers, water pumps, substation, landing beacon |
| Quartz/sand, limestone, clay, soda ash | Glass, silicon-grade stock, ceramics, cement, concrete mix | Window/glass panels, solar cells, printed circuit boards, cameras, displays, plant pots, construction |
| Timber and plant fibers | Boards, textile, paper, packaging | Furnishings, bed/textiles, clothing, books, printed documents, government/education supplies |
| Hydrocarbon/chemical feed | Polymer, insulation, fuel, mechanical/electrical packaging | Insulated cable, components, computer/media devices, hygiene, power-generation consumable |
| Harvested grain, vegetables, livestock products, milk, untreated water | Grain/flour/bran, vegetables, dairy, animal protein, treated water | Meal preparation, shared dining services, downstream food distribution |
| Plant cuttings, finite substrate and pots | Nursery stock, potted live plants | NPC housing/clinic/school/office comfort and landscaping demand |
| Finite waste co-products | Tailings, biomass residue, food scraps, dirty water, process gases | Future recovery/compost/water systems; all mass **retained in explicit candidate material reservoirs** rather than erased |

The same `part/circuit_board` appears in computers, communication nodes, radios, cameras, video cameras, printers, and medical scanners (via computers). A chair is a single generic durable category regardless of whether a restaurant, classroom, office or home uses it. Media equipment is physical; **the concert, film screening, legal consultation, education, laundry and clinical treatment are services**, not stackable magical goods.

## Research provenance and limits

These links ground **the kinds of stages, dependencies and equipment**, not the quantities in the JSON. **All item masses/volumes, example energy usage, work ticks and conversion yields are tentative GAME DESIGN BALANCE**, not a USGS/USDA/DOE physiological or chemical recipe, manufacturer's bill of materials, or accepted in-game progression rule.

- **Minerals to manufactured goods:** USGS Minerals and Uses (2026), https://www.usgs.gov/programs/mineral-resources-program/minerals-and-uses-activity and 2026 commodity summaries https://pubs.usgs.gov/publication/mcs2026
- **Agriculture to markets:** USDA ERS Resource Requirements of Food Demand, https://ers.usda.gov/data-products/resource-requirements-of-food-demand/documentation; source distinguishes crops, livestock, processing, transport/storage, wholesale, retail and food service. A ranch is an actual animal/feed/water/worker facility, NOT a one-tick meat-generating recipe. The raw livestock carcass input is a **future physical-husbandry handoff**, not something the workshop mints.
- **Material processing heat:** DOE Process Heat Basics, https://www.energy.gov/cmei/ito/process-heat-basics ; real industrial processes vary vastly in temperature, byproducts and equipment.
- **Electrical delivery:** EIA Grid Delivery, https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php ; generated electricity is **power flow through equipment and distribution**, not an inventory item recipe.
- **Electronics:** NIST semiconductor process overview, https://www.nist.gov/publications/innovations-advanced-processes-and-systems-semiconductor-manufacturing ; `bulk/silicon_grade` to a finished PCB or solar cell is an intentional high-level abstraction that **still requires** a specialist precision-fabrication machine/worksite and testing before gameplay approval.
- **Health:** WHO Essential Medicines (2026), https://www.who.int/news-room/fact-sheets/detail/essential-medicines ; the candidate has sterile dressings, first-aid kits, beds/scanners and clinical service, **not invented medicinal compounds or medically efficacious recipes**.
- **Product lifecycle:** EPA Sustainable Materials Management, https://www.epa.gov/smm/sustainable-materials-management-basics ; durable goods are not removed from NPC possession per tick. Separate future condition/damage/maintenance/waste.
- **Nurseries:** USDA Forest Service commercial greenhouse planning, https://research.fs.usda.gov/treesearch/31419 ; living plants are durables with ongoing water/light/site care.
- **Laundry:** EPA WaterSense at Work Laundry Equipment, https://www.epa.gov/system/files/documents/2023-06/ws-commercial-watersense-at-work_Section_3.5_Laundry_Equipment.pdf ; laundry transforms **condition of existing textiles**, consuming finite water/detergent/power, not duplicating clothes.

## Correct model of demand

1. **Consumables** (prepared meals, sterile dressing, detergent, printer paper) use up specific physical quantities, including finite production byproducts/waste. Most are *not* acquired individually; groups may access a colony dining facility, clinic, school or laundry.
2. **Durables** (chairs, beds, computers, scanners, cameras, plant pots, potted plants) provide persistent service capacity. Their condition, upkeep, shared access, finite storage and NPC-dependent utility matter; they are not consumed each tick.
3. **Services** (laundry, clinical care, education, legal consultation, government administration, dining, performance and electricity delivery) are **authorized staff/time/infrastructure activities**. Only an actual world service receipt or existing actor effect can affect an NPC's state. Stage 2 decides precise work scheduling.
4. **NPC satisfaction/migration** is *not* implemented by this recipe list. In Stage 7, availability and quality of goods/services, individual traits and expectations, relationships, safety, durable grievances, transport and destination opportunities should shape voluntary mobility. No single chair automatically summons a migrant.

## Integration risks explicitly held open

- **Original world vs candidate:** Current `Inventory::Catalogue` volume-only `liters_per_unit` and original `Simulation` quantities are authoritative; the earlier `EconomyPrototype` 8-item/7-recipe test is a separate in-memory module. This JSON is an **independent research/validation artifact only**, not a second live item ledger. Its **unresolved** legacy bridge blocks any automatic `good/aluminum` to `bulk/aluminum` grams conversion.
- **Original mass/energy:** The candidate's 1-gram bulk accounting and exact input-to-output equality are illustrative. Actual smelting, food growth, burning, pasteurization, cement production and atmospheric exchanges require real source/sink decisions, environmental reservoir debits and grounded yields before authoritative production. Candidate `waste/process_gas` is a **bookkeeping placeholder**, not atmospheric physics.
- **Power and facility machinery:** `energy_kj` and `site_capability` describe prerequisites; they do not create operational generators, casting molds, autoclaves, PCB cleanrooms or ranches. Reuse original constructed shop and native workforce capacity, no arbitrary tier gates.
- **No new game market/currency:** Owner-selected architecture is resource-contribution/consumption driven, no mandatory wages/chits/trading; older ECON docs describing currency are historical suggestions, not commands.
- **Recycling:** This first directed-acyclic graph does not yet contain reverse reclamation that creates source material (would require explicitly bounded loss and item-condition/equipment rules to avoid infinite-yield loops). Recycling must eventually be **strictly less than 100% effective** as previously decided.
- **Farming/animals:** Harvested crops, dairy, livestock products and nursery cuttings are **inputs from future physically tracked land/animal inventories**. Nothing here spawns them. Finish where those quantities originate and how farm/ranch work, water, feed, land and spoilage operate.
- **Safety and baseline:** No PR merge/native acceptance without the original host, authoritative SQLite transactions, Save/Restart, rollback/failpoint tests, mass/volume and worker qualifications. Continuous basic tests remain mandatory even though the owner scheduled major integration testing later.

## Remaining INDUSTRY-01 tasks (still ahead)

1. Trace **all original live item codes, exact masses/quantities, storage volumes and old-save versions**, and produce a conversion/migration ledger, rejecting ambiguous units.
2. Validate researched numerical ranges, processes and alternatives for a small representative subset (e.g. ore → metal → panels, crops → meals, silica → glass → optics, copper → circuit → radio), with physical environmental sinks/sources and suitable building/staff/time requirements.
3. Build a **typed additive C++ recipe registry** or trusted JSON loader into the existing canonical item/industry authority, only after those identities are reconciled; add C++/SQLite regression tests for actual inventory and worker-bound work receipts.
4. Add condition/spoilage, substitutable variants and real service capacity/use semantics **as data contracts**. Implement work staffing in Stage 2 and satisfaction/migration in Stage 7, not prematurely.
5. Run independent native Linux/Windows and long-lived transactional tests before marking Stage 1 production-integrated. Maintain this candidate separately until then.

**Status:** industry work started and first candidate implemented/validated. **Stage 1 is not closed**.
