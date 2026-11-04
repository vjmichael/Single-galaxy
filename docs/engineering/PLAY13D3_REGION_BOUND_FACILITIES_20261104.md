# PLAY-13D.3 — Region-bound planetary construction, farming and extraction

**Implementation:** v77 native-world candidate on draft PR #48. **CI acceptance pending.** This phase attaches finite material custody and facility identity to a specific surveyed `(body, latitude, longitude, generator revision)` cell. It does not yet claim animated planetary landings, autonomous farms, full biomass chemistry, energy balancing, or persistent geological simulation.

## Authoritative records and facilities
- Canonical `Simulation::planetary_sites_` is keyed by `PlanetarySiteKey{body,latitude,longitude,1}`, with owner, initial source ship, installed module tonnage, farm/extractor equipment flags, four separate physical kg stocks, and harvest cycle history.
- `PhysicalCelestialBody` and existing PGF-01B.2 map-backed `Simulation::scan_planetary_geography` remain the sole location authorities. No separate biome generator, phantom planet, or virtual site ID is introduced.
- **Construction:** authenticated real equipped colony ship must be physically anchored to that body, carrying two manufactured modules (4 tonnes) and at least two crew. Must select exposed surface (not an open-ocean cell). Exposed frozen rock/ice can host *sealed future facilities*; no automatic surface agriculture.
- **Farm equipment:** costs another two manufactured modules (4 tonnes), requires PGF's *region-specific* farming suitability, and may only be installed once per region. Repeated actions reject duplicate equipment. Staff remain physical crew on the source ship for this initial gate; autonomous NPC work assignment is not yet implemented.
- **Farm production:** explicitly supplied site water and nutrients are locally transformed using the existing 10 kg water + 5 kg nutrients -> 12 kg food + 3 kg wastewater gameplay recipe. The local farm is not credited with the ship's global store, and harvest does not teleport to the ship.
- **Physical supply/retrieval:** ship and site must be in the same anchored celestial frame, with matching player and ship custody. Supply moves finite water/nutrients from the actual ship life-support stock to the site. Retrieval moves only existing site food/water back to the ship.
- **Resource extraction:** costs another two real modules for extractor equipment. Only the pre-existing, explicitly genesis-provisioned finite oasis reserve can be depleted; `landscape.local_water_potential` or mapped rivers are NOT sources of kilograms. A deterministic singular *source surface region* is chosen from exposed land on that exact body and version. Only an extractor installed at that cell can collect water/nutrients. Open water, polar ice and rivers do not imply free or infinite extractable matter.
- **Player exploration:** native `SYSTEM_CELESTIAL` includes `OASIS_REGION` waypoint hints for known existing physical reserves. A user must explicitly survey the exact target cell, which writes an owner-private immutable `SurveyArchive` record, then build. Native host denies region mutations if the archive has not recorded matching geography.
- **Browser:** collapsed region-operations selector supports construction, farm/extractor installation, stocking, cultivation, source collection and retrieval; the host publishes a committed native receipt only after the same world SQLite transaction completes.

## Recovery, mass and permission invariants
- All six `PersistentWorld` methods make an isolated `Simulation` candidate and commit its v77 canonical state through the existing SQLite generation/failure-injection journal **before** publishing changes. No new disconnected "planet farm" inventory database is used for gameplay operations.
- `total_ore()` includes each facility's exact 4/8/12-tonne embodied module stock plus all local food, water, nutrient and wastewater kg alongside existing ship, outpost and oasis stocks; `total_material_kg` remains invariant across construction, farm conversion and extraction.
- Source reserves can be exhausted. Any physically transferred kg is subtracted from exactly one source and added to exactly one destination. Built sites persist even when their founding ship leaves the planet, and off-planet commands fail.
- Owner mismatch, forged body, invented region, stale version, no installed farm/extractor, no source reserve, wrong material, negative/excessive quantity, missing modules or absent source ship location fail closed.
- v77 snapshot parser rejects invalid regional stock/flags, duplicate records, tampered region keys and orphaned astronomical bodies. Older saved worlds continue to load; they do not automatically fabricate planetary facilities.

## Native acceptance added
- `PLAY13D_RegionalFacilitiesTests.cpp`: find a real eligible physical region; manufacture source modules using actual native mine/refine/fabricate; arrive through durable celestial transit; inject snapshot-write failure and require no mutation; build, equip, supply, grow or extract, retrieve and verify no mass change; depart, deny remote transfer, reload SQLite and verify v77 canonical state.
- `PLAY13D_PlanetSurveyNodeTests.js`: parse and validate committed receipts and finite source waypoints; reject malformed/forged telemetry.
- `PLAY13D_PlanetSurveyRelayTests.py`: HTTP strict schema, quantities, origin and CSRF denial across every new regional action.
- `play13d.yml`: Windows/Linux native build and CTest, Node contract and HTTP/native host smoke. **Native runs must pass at exact latest PR head before declaring acceptance.**

## Remaining engineering gates
1. Real native Windows/Linux CI compilation and failure triage at exact SHA.
2. Real **end-to-end host** acceptance of `PLANET_BUILD` through `PLANET_RETRIEVE` (direct `PersistentWorld` integration tests exist, but host workflow still needs a full physical gameplay smoke with actual construction receipts).
3. Persistent world-genesis *regional* mineral/water deposits (current finite source is still an existing body-level reservoir with one deterministic surface waypoint). Site-specific extraction tooling must migrate to independently inventoried local aquifers/mineral seams.
4. Planetary descent/landing, surface transport and actual worker assignment/return; anchored celestial frame alone is not proof of a traversed planetary surface path.
5. Labor, repair, power, seasonal irrigation, atmospheric differences and proper closed-loop nutrition/wastewater/compost mechanics. Current farm recipe preserves matter but is deliberately simplified and does not model crop energy or full chemistry.
6. Exactly-once request ID receipts; current duplicate **construction** is denied but repeated valid cultivation/extraction messages are separate operations, not cryptographically/replay-bound attempts.
7. Replace derived climate envelopes with immutable world-genesis versions and reconcile archived regional surveys when geography algorithms evolve.
