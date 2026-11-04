# PLAY-13D: Planetary environment generation principles

**Design decision (2026-11-04): A planet is not a biome.**

A planetary climate archetype is a *global constraint/distribution*, not a uniform texture or ecosystem. Regional conditions emerge from atmospheric circulation, latitude, elevation, land/sea boundaries, water transport, river basins, subsurface heat, and local geological conditions.

- **Diverse worlds:** Life-supporting, arid, oceanic and frozen worlds may contain extensive regional differences. A fertile world includes infertile places; an arid world may have high/low deserts, mountains, prairies, dry arroyos, perennial rivers, forested canyon refuges and illuminated groundwater-fed cave vegetation; a frozen world with tropical temperatures near 0 C (32 F) may contain much colder poles below -51 C (-60 F) while sheltered geothermal, brine, or subglacial niches might support different life.
- **Constrained worlds:** The complete planet may be limited to a narrow thermal regime or scarce rainfall, but localized microclimates and ecological opportunities can exist. A cold planet does not imply that Earth-like surface vegetation is viable. Possible life does not imply confirmed life.
- **Nearly mono-environment worlds:** Some atmosphereless rocky worlds and similarly sterile worlds can be nearly uniform in their global environment. Physical geography, crater fields, caves, slopes and mineral distributions may still vary. Do not inject vegetation or habitable oases to satisfy a variety quota.
- **Simulation rule:** Diversity is an emergent consequence of conditions, **not** a minimum biome count. Never force forests on every world or assume deserts are sand.
- **Survey rule:** Players discover actual regions, water/energy availability, soil and construction potential, not simply a one-word planetary biome. Surveys inform local settlement trade-offs.
- **Persistence rule:** Geological profiles and generated regional maps must remain stable across restart and generator upgrades. Water, biological biomass, mining and construction must use durable material custody and cannot be inferred from mere visual classification.

## Prototype / acceptance boundary

Existing PLAY-13D.2A/B/C survey layers are coarse procedural examples. They do not yet compute real connected watersheds, geological epochs, atmospheric energy budgets or ecological dynamics. An initial PLAY-13D.2D frozen-world header was added, but ice niche assignments remain **potential**, not physically simulated biological organisms or liquid-water inventories. No native CI acceptance is claimed for those gates until exact-head tests pass.

## Next technical gates

1. Replace global hard-coded terrain thresholds with a seed- and physics-constrained planet generator and persistent versions.
2. Implement elevation continuity, linked drainage, rain shadows, ice and subsurface thermal modeling.
3. Distinguish physical liquid water/groundwater and actual biosphere presence from purely environmental *potential*.
4. Bind local construction, farms, extraction, logistics and survival to actual surveyed sites.
5. Test both high-diversity and nearly mono-environment worlds without arbitrary biome quotas.
