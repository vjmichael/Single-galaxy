# PGF-01B.2 — Single-Source Planetary Geography

**Code:** integrated geographic site/PGF survey API committed to PR #48. **Native exact-head acceptance pending.** No host-facing construction or water inventory changes are claimed.

## Core contract
- `WatershedMap`, generated once from `PlanetaryWorldKey` + `PlanetaryEnvelope`, is the **sole authority for candidate regional elevation, ocean mask, downstream routing, precipitation accumulation and watercourse classification**.
- `survey_geographic_site(map,key,envelope,lat,lon)` projects local landform, slope-relative canyon potential, arid prairie, mountain and river corridor from the precise map cell and its downstream neighbor.
- `survey_planetary_world(map,key,envelope,lat,lon)` is the unified map-backed PGF client entry point for terrain/ocean/survey. The older three-argument `survey_planetary_world(key,envelope,lat,lon)`, `survey_region` and `survey_landscape` are **legacy prototypes**, retained to keep existing tests and callers functioning. Gameplay consumers must use the map-backed overload. Migrating all remaining callers is a separate acceptance requirement.
- A map rejects mismatched galaxy, system, body, generator revision or climate envelope. Same cell from landscape and PGF reports matching terrain/elevation/ocean classifications and consistent downstream target.
- In a frozen world, drainage scores do not imply liquid flow: ice world waterways are suppressed pending thermal/hydrological seasonal simulation. Ocean basin under ice is not labeled an exposed liquid sea.
- Airless worlds do not acquire free greenery or water. Lifeless environments do not have procedurally fabricated vegetative mass. In-canyon forest is an ecological **potential**, not a conserved source of harvestable wood.
- Generic cave/light/groundwater guesses from the prototype are suppressed in the authoritative geography view. Cave and aquifer networks need independent physically coherent geology, lighting, and custody.

## Test scope
`PLAY13D_PlanetaryGeographyTests.cpp` checks all 2592 regional cells per test planet, across temperate, arid, frozen, and sterile-world envelopes; verifies map-backed PGF/landscape parity, exact downstream cell references, flow class correspondence, ocean coverage, no free vegetation or liquid in frozen worlds, and rejection of unrelated keys/envelopes. Wired to `galaxy_play13d_planetary_geography_tests` in Windows/Linux `play13d.yml`.

## Remaining blockers to final PGF-01B.2 closeout
1. Native Windows/Linux acceptance at exact head, including regression for older PGF-01A/B tests.
2. Route every *actual* exploration, scanner and client projection to the map-backed API; explicitly retire or mark legacy pathways non-authoritative. Current code adds the canonical API, but does not claim complete client migration.
3. Persist the generator revision, map inputs and observed site commitments to SQLite. Existing worlds must retain their terrain across generator upgrades.
4. Coordinate watershed-ocean classifications with real `PhysicalCelestialBody` geology, soil, planetary water mass and genesis source allocation.
5. Model channel widths, ocean depth/bathymetry, freshwater topology and true canyon morphology without allowing mutually contradictory site inventories.

Until these are done, this is a **candidate geography integration gate**, not production planetary settlement or fully closed PGF-01B.2.
