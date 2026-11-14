# Single-Galaxy — Planetary Agriculture and Closed-Loop Nutrient Return

**Design decision:** Planetary mining, farming, orbital food supply and return logistics share one durable physical material economy. Player-built and NPC-managed colonies cannot silently erase waste. Players may call the reciprocal nutrient-return contract **Cosmic Law** (informal gameplay name).

**Status:** Design recorded, NOT an implemented C.5 subsystem. Existing life-support stocks and recycling operations are a foundation; do not claim that interplanetary farming, shipment provenance or physical return cargo are already integrated.

## 1. Scope and principle

Planets and moons may have finite, surveyed regional geological reserves, soils/substrates, extractive mines, agricultural facilities, water sources and environmental constraints. Their mineral profiles are derived from the existing `AsteroidClass` and `mineral_profile` game-data scaffolding, but **planetary soil, deposits and cultivatable resources require their own embodied inventories**; a local asteroid deposit alone does not represent farmland or all planetary reserves.

Space colonies may import physically hauled food, water, oxygen or other agricultural outputs from an identified producing planet, settlement or orbital producer. Consumption must partition inputs into conserved outputs (biomass growth or inventory, breathable or other gas, CO2, organic solids, wastewater and recoverable nutrients), with any irrecoverable gameplay waste remaining in tracked environmental sinks rather than disappearing. Recyclers may recover material locally.

**Conservation law:** across every production, consumption, transfer, composting, nutrient processing and shipment transaction, input mass equals accounted output mass plus any precisely tracked environmental sinks. Account for external water, oxygen and other reagents: wet-food mass and returned dry fertilizer mass are not equal by default.

**Gameplay reciprocity policy:** a producer can demand the return of equivalent *recoverable agricultural nutrient value* from destinations importing its food. This is a contract/policy rather than a claim that real-world conservation forces matter to travel back to its origin. Do not automatically confiscate the buyer's resources or teleport waste to the origin.

## 2. Physical cargo and provenance

Persist a ledger entry for each transfer lot:
- Unique `lot_id`, producing celestial body/settlement, current physical custodian, exporting entity, receiving entity, material kind, measured mass in kg, nutrient accounting units and provenance chain.
- For foods, register nutrient basis values (e.g. nitrogen, phosphorus, potassium, organic carbon in *defined integer units*) alongside total wet mass and composition. No floating-point conservation ledger.
- Production and conversion lots never create additional material. The batch must cite an actual debited site inventory, environmental withdrawal, transported reagent, or prior lot.
- Parent/child lot lineage and embodied NPC/cargo custody survive atomic SQLite world commits, restart, recover-copy and migration.
- World partitions and celestial reference frames govern proximity: a transaction cannot exchange inventory between distant frames. All transfers require a real ship, freight manifest, authorized cargo operation and passage/arrival custody.
- Returned waste is not necessarily safe farm fertilizer: require composting, biological treatment, sterilization or nutrient-recovery workflows before soil application. Wastewater can be processed into clean water and recovered nutrients; hazardous concentrates remain classified custody, not edible food.

## 3. Planetary mining and agriculture

Planetary surveys expose independently persisted resource estimates with uncertainty; exact mineral grades are not revealed before sufficient sampling. Geological bodies have finite material, physical deposits, type-dependent grade and accessibility, and cannot respawn when the explorer returns.

Agricultural zones consume site-local water, atmosphere/CO2 where relevant, light/energy, seed and substrate nutrients. Production depends on environmental suitability, staffing and infrastructure. Farm nutrient balances decrease with harvest/export and recover when valid compost/nutrient returns arrive and are actually applied. Imported supplies must never appear automatically in the colony's stores.

Settlement food, waste and fertilizer warehouses are distinct physical locations with explicit hauling, capacity, sanitation, processing power and NPC workload. Avoid excessive micro-management by keeping NPC-managed default logistics, transparent bottlenecks and manual overrides.

## 4. Cosmic Law contract / quota behavior

For every accepted food import, the receiving colony accumulates a *nutrient-return obligation* measured from actual exported nutrients, net of documented local loss and pre-agreed recyclable fractions. Do **not** set obligation equal to imported wet-food kilograms. Policy may distinguish:
1. **Open-loop** — producer knowingly exports nutrients permanently, reducing its long-term soil reserve.
2. **Reciprocal** (default for participating planetary farms) — destination commits a nutrient-equivalent return flow in processed fertilizer, suitable biosolids or other approved agricultural nutrients.
3. **Retained locally** — destination obtains producer approval to keep recovered nutrients for its own local agriculture, recording the producer's accepted permanent export.

A due obligation cannot be settled by merely generating a UI receipt. Settlement requires real eligible nutrient mass physically delivered to the designated planet or a contractually agreed exchange depot, accompanied by an accepted quality/specification and chain of custody. Partial returns reduce outstanding units; quality rejection leaves the shipment and obligation intact.

Failed shipping, cargo destruction, contamination, diverted nutrients or shutdown do not erase material obligations. They update the durable liability and local environmental sink history. Make obligations economically meaningful through continued supply, farm viability and NPC trust/negotiation, **not** automatic character death or unsatisfiable instant penalties. Autonomous freight and NPC managers should be capable of handling recurring pickups when infrastructure exists.

## 5. Conservation example (illustrative, NOT a finalized recipe)

A planetary farm exports 1,000 kg of wet food with an independently measured nutrient ledger. The orbital colony consumes it. Its system partitions that input, together with measured breathing oxygen and other inputs, into retained biomass/inventory, CO2, recovered water, organic solids and other tracked outputs. It may recycle water and some nutrients internally. The planet's return contract measures only the agreed recoverable nutrient units. A 1,000 kg return of raw sewage is neither automatically required nor automatically sufficient. Returning a smaller mass of safe, concentrated nutrients may satisfy the same farm obligation.

No exchange should count both food consumption and later waste processing as new matter. No landfill, sewage release, burning or air venting without a tracked sink or emitted mass.

## 6. Implementation gates, in dependency order

- **AGR-RET-01:** Audit existing `ColonyLifeSupport`, `FarmFacility`, `HabitatFacility`, site stores, waste stocks, resource-origin conservation and cross-colony hauling. Explicitly map every current consumption/recycling action's debit, output and sink before introducing planetary exchange.
- **AGR-RET-02:** Define stable integer-based lot composition, location and origin provenance; test multi-step partition and material conservation, save/restart, invalid and duplicate lots, exact mass.
- **AGR-RET-03:** Introduce finite planetary soil/mineral/water nutrient stockpiles, site-local mining and farms, fertilizer application, immutable harvested lots and non-regenerating depletion, with physical origin and no client-created resource.
- **AGR-RET-04:** Hauling/manifest custody from farm to colony; real consumption and generated recyclable outputs; existing waste stocks must be credited, never merely incremented for storytelling.
- **AGR-RET-05:** Reciprocal producer agreement, carried nutrient obligations, real return transport, grade/sterilization and producer acceptance, partial settlement, idempotent receipts and dispute/missing-freight handling.
- **AGR-RET-06:** NPC background logistics and viable open/reciprocal/retain policies, failure recovery and transparent GUI. Verify native Windows/Linux cross-platform, fault injection and multi-colony conservation.

### Current foundation

- Mineral profiles: `src/Simulation.cpp::mineral_profile`, `include/galaxy/Simulation.hpp::MineralFraction`.
- Native v74 locally differentiated ore classes and conservation: `docs/engineering/PLAY13C5_CELESTIAL_ENVIRONMENT_GEOLOGY_20261104.md`.
- Existing life support: `ColonyLifeSupport` includes `food_kg`, `water_kg`, `co2_kg`, `compost_kg`, `wastewater_kg`, `organic_waste_kg`, `sludge_kg` and recycling actions.
- Planetary agriculture, lot provenance, nutrient-value reciprocity and real waste return **are not yet implemented**.
