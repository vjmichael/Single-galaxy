# Single-Galaxy Stage 1 — Real-world supply-chain and recipe research
**Initial research brief, November 6, 2026.** Research-informed architecture and candidate *game* recipe families, **not** a final mass-calibrated production dataset or claims of completed runtime integration. Owner's binding 20-stage roadmap: [PROJECT_DEVELOPMENT_SEQUENCE_20261106.md](PROJECT_DEVELOPMENT_SEQUENCE_20261106.md). Industry precedes NPC work, settlements, economic allocation, transport integration, planets, population, travel, security, art and player gameplay.

## Research-based generalizations

1. **Food is multi-route, not a single farm-to-market item.** The USDA Economic Research Service tracks agribusiness, crop and livestock production, processing, transportation/storage, wholesale, retail and food service, including energy and support services. In Single-Galaxy, a crop can go straight to a household/restaurant, or pass through processing/cold storage first. A ranch needs feed/water, husbandry labor, animal health, storage and cold chain. Do NOT model every food type.
   - USDA ERS: https://ers.usda.gov/data-products/resource-requirements-of-food-demand/documentation
2. **Industrial families share underlying materials and operations.** USGS's 2026 Minerals Commodity Summaries covers 90+ mineral/material commodities; quartz/silica supplies glass and silicon; copper supplies wiring; limestone/calcite and gypsum supply cement/building boards; phosphate supplies fertilizer. The game should group by **functional material**, not simulate every real mineral in every rock.
   - USGS 2026: https://www.usgs.gov/centers/national-minerals-information-center/mineral-commodity-summaries
   - USGS minerals and everyday uses: https://www.usgs.gov/programs/mineral-resources-program/minerals-and-uses-activity
3. **Heat, power and equipment matter to production.** DOE distinguishes industrial process heat from other energy use and identifies food, chemicals, steel, cement and forest-product operations as energy-intensive. Recipe cost must track power availability, heat capability, running time, machine life and actual input material, rather than fictional machine tiers.
   - DOE process heat: https://www.energy.gov/cmei/ito/process-heat-basics
   - DOE energy-intensive industries: https://www.energy.gov/cmei/ito/energy-intensive-industries
4. **Power networks aren't finished goods alone.** EIA models generation, high-voltage transfer, substations/transformers and distribution to consumers. Treat generator/transformer/cable/switchgear/battery as manufactured durable items; **electricity supply is a time-varying utility flow**, not a free item in a cargo hold.
   - EIA: https://www.eia.gov/energyexplained/electricity/delivery-to-consumers.php
5. **Healthcare needs supplies, care labor and verified preparation.** WHO deliberately selects a manageable essential-medicines subset instead of enumerating every medicine. FDA describes distinct sterilization requirements for medical devices. Start with grouped medical consumables, diagnostic devices, clinic equipment and practitioner services; no simulated reaction-by-reaction pharmaceuticals or unlimited magical healing.
   - WHO: https://www.who.int/news-room/fact-sheets/detail/essential-medicines
   - FDA: https://www.fda.gov/medical-devices/general-hospital-devices-and-supplies/sterilization-medical-devices
6. **Electronics have distinctive actual stages.** NIST/OECD distinguish semiconductor design, fabrication, packaging and test from board/product assembly. Gameplay should retain the meaningful high-cost precision/cleanliness/chemistry/equipment dependency without hundreds of photolithography substeps.
   - OECD: https://www.oecd.org/content/dam/oecd/en/publications/reports/2024/08/chips-nodes-and-wafers_1189c2a2/f68de895-en.pdf
7. **Repair/reuse/recycling are integral to goods demand.** EPA's material-life-cycle guidance covers extraction, manufacture, use, maintenance, reuse, recycling, and waste. A *chair* is durable property occupying space; it improves usable communal/housing capacity and eventually wears. It is not simply eaten by an NPC each tick.
   - EPA: https://www.epa.gov/smm/sustainable-materials-management-basics
8. **Factory Simulator is a visual lesson, not an economic template.** Its cutter, furnace, press, extruder and assembler chains show recognizable processing. Do not copy the tier-price treadmill, fictional supermaterials, or mandatory upgrade levels.
   - Community machine table: https://roblox-factory-simulator.fandom.com/wiki/Machines

## One reusable industrial graph

The world has **material families** and their actual durable inventory, not independent sector-specific inventories. One original site/work order consumes source items and outputs known items/byproducts, scheduled with existing named NPCs and finite equipment/power.

```text
finite geology / atmosphere / water / biomass
       |
       +-- metals ----------------> sheets / extrusions / fasteners / wire ------+
       +-- silicates -------------> glass / ceramic / cement ------------------+
       +-- petro/biomass chemistry -> polymers / fibers / detergents -----------+--> common finished goods
       +-- crop/livestock inputs --> raw food / feed / fibers ------------------+
       +-- quartz / minerals -----> electronic-grade stock / components -------+
       +-- building timber/plants -> boards / paper / nursery stock -----------+
                             |
                      equipment + skilled time + power/heat + transport
                             |
         housing / utilities / healthcare / transport / communications /
         entertainment / hospitality / legal and public services
                             |
              resident use / repair / waste sorting / recycling
```

**No universally prefabricated market, police fleet, factory or food-service outlet at genesis.** One colony ship has finite bootstrapping tools, materials and crew. Every downstream workshop, utility, outpost, medical center or transportation network must be constructed/furnished by player direction or an explicitly approved standing work program.

## Initial sector-to-demand research map

| Sector | Candidate producer → intermediate → usable output | What actually creates ongoing demand |
| --- | --- | --- |
| **Food and agriculture** | Farming: water/nutrients/seeds → crops → grain/flour/vegetables → packaged food or restaurant meals. Ranching: feed/water → livestock → eggs/dairy/meat products (with lawful finite custody). | Survival, health, nutrition diversity, cooking, dietary preference, restaurant service, spoilage and waste. |
| **Medical and health** | Fibers/polymer/glass/metals → bandages, sterile supplies, diagnostic tools; controlled chemicals → grouped essential medicines; clinic with trained staff. | Treatment episode consumes genuine supplies; durable bed/scanner lasts and needs maintenance; clinic quality/capacity constrains health outcomes. |
| **Construction and housing** | Limestone/clay/heat → clinker/cement; aggregate/sand/water → concrete; metal ore → structural alloy → beams/panels; lumber → boards; glass/polymer → glazing. | Habitation space, repairs, climate control, safety, utility pipe/wire installation; no infinite decor demand. |
| **Furniture and personal comfort** | Boards/metal/polymer/textile → chairs, tables, desks, bunks, storage; horticultural substrate/seed/water → potted plants. | Seating/bed/storage accessibility, ergonomic/social/aesthetic preferences, wear and maintenance. Shared facilities can satisfy a group; do not require one of each object per NPC. |
| **Energy and utilities** | Metal core/copper winding/insulation → generator, transformer, switchgear, cable; fuel/solar source/storage → electrical generation/distribution, water pumping/treatment/HVAC. | Continuous kilowatt demand, peak capacity, heat losses, machine upkeep, stored fuel/battery materials and service continuity. |
| **Travel infrastructure** | Structural metals/concrete/cables/computers → roads, ports, berths, launch/landing equipment, navigation and comm beacons, freighters, replacement parts. | Real routes, wear, maintenance, security/port operators, passenger/cargo throughput and local physical location. |
| **Communications/electronics** | Copper → wire + insulation → cable; glass/silicon → optics/chips; PCB + components → computer, router, radio, antenna. | Network operation, distance coverage, bandwidth/uptime and physical signal/energy limits, upgrades/repair. |
| **Entertainment/media** | Electronics/optics/metal/polymer → speakers, microphones, cameras, video cameras, projectors/screens, instruments, printed art/book/media stock. | Leisure and creative/social opportunities; equipment is durable; live performances/recordings consume actual labor and power, not a new material for every fictional song. |
| **Services** | Machines + water/detergent/electricity + staff → clean laundry; tools/spares + staff → maintenance; prepared food + staff → dining; training time → education; papers/records or computer + staff → legal advice. | Service capacity, cleanliness/maintenance, satisfaction, business access and queues. **Service outcomes are not inventory objects.** |
| **Government/community** | Constructed office, desks/computers, records/security equipment, communications, staff → administration/public information/legal processing; emergency supplies and assigned staff → safety/rescue capacity. | Resident/colony needs and authorized policies, accountable staffing, infrastructure and actual case events. No imaginary paperwork recipes, instant police or paperwork as a happiness tax. |

The **same original item** may satisfy multiple sectors. E.g. copper wire is used by an electric utility, computer, loudspeaker, security camera and shipboard electrical system. A physical chair can furnish a school, office, restaurant or home without five redundant chair SKUs.

## Illustrative candidate recipes (symbolic; NOT yet numeric game formulas)

| Canonical operation | Input families, actual capacity | Output / accounting |
| --- | --- | --- |
| `smelt.copper` | graded copper ore + process heat + extractor | copper metal + waste mineral/slag (and process emissions where modeled) |
| `draw.copper_wire` | copper metal + drawing tool + energy + labor | bulk copper wire + offcut scrap |
| `insulate.electric_cable` | copper wire + insulating polymer + machinery | insulated power/comms cable |
| `fabricate.panel` | refined structural metal + forming/welding work + power | panels/beams + recyclable cutting scrap |
| `make.cement` | limestone/clay + kiln heat + grinding/gypsum | cement binder + accounted exhaust/emissions; raw limestone mass never vanishes |
| `mix.concrete` | cement + aggregate + sand + water + mixer | limited-life wet concrete material + process waste; installed structure created by a separate build order |
| `grow.grain` | seeds + water/nutrients + CO2 exchange + light + growers/site | harvested grain + biomass/residue + water exchange; account mass through actual reservoirs |
| `mill.grain` | grain + mill + energy + worker | flour + bran/feed/co-product |
| `prepare.meal` | edible produce/protein + clean water + cooking heat + cook | meal serving; short shelf life, consumed by actual NPC household/dining service |
| `fabricate.furniture` | boards or sheets + fasteners + fabric/padding as applicable + bench | chair/table/desk/bunk family; durable installable item, not consumed per tick |
| `grow.decorative_plant` | cutting/seed + container + growing substrate + finite water/light/time | potted living plant with maintenance needs and finite carrying capacity |
| `fabricate.pcb` | conductive copper + board substrate + electronic components + specialized equipment | assembled circuit board; electronic-grade inputs imply prerequisite precision capacity |
| `assemble.computer` | assembled PCB + chips/memory + housing + screen or terminal interface + power module | functional computer, durable and repairable |
| `assemble.audio_video` | assembled PCB + acoustic transducer or optical lens/sensor + casing + cable | microphone / speaker / camera / video camera (separate meaningful equipment capabilities) |
| `assemble.printer` | electronics + motor/mechanical feeder + housing | copier/printer durable device; using it consumes paper, pigment/ink/toner and electricity |
| `prepare.medical_dressing` | textile/polymer + wrap + clean equipment + validated sterilization | sterile consumable kit; finite stock/shelf life |
| `produce.power` | installed source + functioning generation/conversion equipment and its fuel/maintenance | **power flow**, not a fabricated object or matter increment |
| `service.laundry` | dirty physical textiles + water + detergent + power + trained service time | same cleaned textile identities + waste-water/heat; no phantom replacement clothes |
| `service.legal` | qualified person + office/communication equipment + optional real records supplies + time | authenticated legal consultation/case-work capacity, **not a pretend item** |
| `assemble.ship_system` | hull panels + mechanical systems + electronics + modules + actual berth/workers | one real assembled component/hull; original yard commissioning/pilot/crew constraints still apply |

Detailed bills of materials and yields must be researched and **balanced** before implementation. Realistic dependencies do not mean a chemically exact recipe for every object: only include a new operation when it changes a material bottleneck, equipment capability, required worker role, quality/safety, output use or meaningful waste/recycling flow.

## Data contracts to reconcile with ORIGINAL engine

- **Canonical `ItemDefinition`** (one namespace/code): family, unit `kg/litre/count`, **both mass and volume**, phase/durability, storage rules, quality/category tags, permitted uses, spoilage/wear, recycler and links to original `Inventory::Catalogue` + native materials. Existing units include legacy game-volume item stacks versus newer kilogram-based freight; never equate 1 litre to 1 kg for all substances.
- **`RecipeDefinition`:** id/version, accepted alternatives, input item/quantity, output item/quantity, co-products and emissions/waste, primary process (melt, mill, assemble, cook, sterilize, grow, etc.), material/energy/time budget, machine/utility capability, safe/unsafe worker qualification, lot size and location/atmospheric constraints. No arbitrary tier-price multipliers; higher tech exists only where physically required.
- **`ServiceDefinition`:** type, equipment/utility requirements, staff/qualifications, throughput/time, consumables, client eligibility/need, durable result/quality/possible aftereffects. Service is a capability/work receipt, not an item that must be hauled.
- **`UseProfile`:** survival, health, shelter/comfort, hygiene, leisure/social, communication, education, safety and culture *separate from worker output*. NPC traits/expectations modify weight and substitution; actual available shared equipment/services and household access satisfy demand. Durable goods wear/repair, supplies are consumed physically, some needs cannot be purchased.
- **`WorkCapability`:** one original NPC time slot, role/skills, supervising NPC when needed, actual location, tools/fixture, consumption and effect receipt, duration/cancellation/recovery. Stage 2 extends existing `WORK-01`, no shadow tasks.
- Every recipe must have **conservation accounting** for all physical stocks and environmental exchanges; process energy is not matter. Return air/water/biomass/solid waste to known reservoirs or explicitly documented controlled abstractions. Durable installation transfers the item into constructed fixture custody; destruction/salvage/recycling return less usable material than was installed.
- Authoritative content is **versioned data** (candidate JSON/CSV tables ingested into validated canonical registries), not dozens of hardcoded disconnected methods. Runtime can choose quantitative balances after data-format/version tests; simulation must reject duplicate item IDs, unknown inputs, unintended cyclical dependencies, zero/negative output, infinite yield loops, mass/volume overflows and nonexistent processing equipment.

## Bounded catalog proposal, not a fixed requirement

For a **first approved full cross-sector pass**, target approximately **80–120 functional item families**, **60–100 meaningful conversion recipes** and a small number of **service/work capability classes**, drawn from the existing game's real item catalogue. Treat as an initial **complexity budget**, not a permanent game limit or a mandate to create filler until a quota is reached.

Selection rule for a proposed item:
1. Does it have a distinct real use, NPC preference, infrastructure prerequisite, maintenance/spoilage characteristic, or transport/resource bottleneck?
2. Does it require a meaningfully distinct material, equipment or professional capability?
3. Would modeling it change a decision, shortage, profession, facility, or colony behavior?
4. Could a quality/appearance variant on an existing family do the job instead?

If only cosmetic, model as variant/tag and art choice; do NOT grow hundreds of mandatory recipes. Suggested shared personal possessions: **bed, seat, table/desk, storage, clean clothing/textile, basic hygiene supplies, home plant, books/media access and optional personal electronics**, not hundreds of decorating SKUs.

## NPC consumption and the eventual migration feedback loop

- **Survival essentials:** accessible water, food, breathable atmosphere, waste handling and suitable shelter; these should remain first-priority production/resupply.
- **Health and maintenance:** finite treatment, sanitation, equipment function and built clinic access.
- **Comfort and belonging:** bed/seating/space/privacy, meaningful activities, education, leisure, personal preferences, safety, social ties and perceived fairness; shared public facilities can satisfy many people.
- **Personal traits** weight different wants. A botanically inclined NPC may value potted plants or a garden; another may barely care. The same good may produce different satisfaction outcomes depending on context, repair condition, accessibility and expectations.
- **Unhappiness is not an instant migration command.** It accumulates contextually alongside credible destinations, physical transport, family obligations, opportunity costs, and actual migration admission and custody. An individual may stay unhappy, adapt, protest, resign, organize or leave; avoid one generic happiness bar that erases identity.

## Stage 1 research execution and exit

1. **Audit current item materials and recipes** across `Inventory.hpp`, `EconomyPrototype`, `Simulation`, `Industrial/Catalogue` and the actual original item mass/volume/energy units. Produce stable item IDs and migration map; keep legacy saves valid.
2. **Trace functional cross-sector industry chains** from raw material and environmental input to finished durable, consumable and public service, marking shared components and missing prerequisites.
3. **Research representative real bills of materials/operations** and tag each source/evidence confidence. Do not present the illustrative graph above as numerically authoritative chemistry.
4. **Choose bounded catalog** and classify which proposed products deserve separate recipes vs parameterized family variants or service outputs.
5. **Specify data schemas and test constraints**, including mass/energy/time, owner authorization, atomic work receipts, byproducts, damage/reuse, spoilage and historical schema versioning.
6. **Define NPC use/demand effects** and what future worker capabilities must exist; defer scheduler implementation until stage 2.
7. **Publish a validated catalog and recipe graph** with automated cycle/conservation/legacy-ID checks. This defines Stage 1 completion, not the implementation of every production machine in the world.

**Current status:** initial research and architecture/specification documented; **catalog audit, numeric real-world bills, code registry and native acceptance remain unfinished**. This brief is a starting point, not a claim that stage 1 is complete.
