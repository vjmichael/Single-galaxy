# PGF-01B.2 — Native Exploration and Planetary Survey Migration

**Implementation checkpoint:** source changes committed to PR #48; exact-head native CI acceptance pending. This is the first live native route, not a complete planetary settlement or material-ecology simulation.

## Runtime flow

1. User selects *Survey equatorial region* on a **real planet or moon** in the existing physical celestial catalogue.
2. `viewer/colony.js` calls `GalaxyColonyCommand('colonyPlanetSurvey',{body,latitude,longitude})`.
3. `viewer/interactive.js` sends the authenticated session JSON through `/command`.
4. `live/interactive_relay.py` checks strict keys, integer types and bounded coordinates then emits `PLANET_SURVEY|body|lat|lon`.
5. `src/colony_live_host.cpp` validates numbers and delegates to `Simulation::scan_planetary_geography(owner,ship,body,lat,lon)`.
6. `Simulation` reuses the existing owner, fitted exploration scanner and physical celestial catalogue to validate **both the player and the body**, derives the fixed version-1 planet envelope from stable astronomical body metadata, builds the deterministic watershed and projects `survey_geographic_site` from that map.
7. The host sends `PLANET_REGION|body|lat|lon|elevation|ocean|terrain|watercourse|drainage|down_lat|down_lon|revision`; the JS telemetry decoder rejects malformed or over-limit data and displays it as geography, explicitly *not* extractable water.
8. Any simulation consumer requiring PGF geography must use `Simulation::scan_planetary_geography` / `survey_planetary_world(map,...)` rather than independently running the older proto-survey helpers.

## Stable data and authorization

- Existing v70+ `PhysicalCelestialBody` IDs remain the sole celestial identifiers. No new body IDs, materials, vegetation, source inventory or NPCs are created by reading this map.
- PGF v1 geography is presently **derived** from a hard-coded astronomical galaxy seed and the immutable system/body identity. The climate envelope is a deterministic prototype, not yet persistently migrated world genesis, and its native resource suitability must not be interpreted as physical water availability or guaranteed biosphere.
- An unrelated owner, a ship without a real exploration scanner, non-planetary body, nonexistent body, invalid region, or altered body ID must fail without a report. The host's signed-64 integer parser was updated from an erroneous nine-digit maximum so real multi-billion-valued celestial IDs are accepted.
- Current single-coordinate query does not yet persist a player-specific discovery receipt in `SurveyArchive`. This remains necessary for surveyed-region knowledge tracking, ownership transfer and partial map revelation.
- Existing **local asteroid/oasis resource** methods remain governed by their own saved v74–v76 world inventories. They have not yet been redefined to use PGF water/soil/land categories; no automatic cross-system resource migration is claimed.
- All new planet-region UI and native telemetry derive their elevation, coast, river type and drainage from the **same map-backed API**.

## Verification

- `PLAY13D_LivePlanetSurveyTests.cpp` uses real `Simulation` and `PersistentWorld`, checks owner denial, forged body/coordinates, same-query parity and SQLite restart parity.
- `PLAY13D_LiveHostPlanetSurveySmoke.py` starts the actual native host, obtains a real planet from `SYSTEM_CELESTIAL`, requests `PLANET_SURVEY`, compares output after native host restart and rejects invalid requests. Explicitly checks body IDs larger than nine digits.
- `PLAY13D_PlanetSurveyNodeTests.js` verifies `PLANET_REGION` parsing, tamper/overflow denial and relay/browser command wiring.
- Windows/Linux `play13d.yml` builds `galaxy_kernel` and actual `galaxy_colony_live_host`, runs CTest, Node, and native host smoke.

## Remaining closeout work

1. Exact-head native CI pass and review any host/relay/browser regressions.
2. Add real player-controlled latitude/longitude region selection and surveyed-map display instead of the current fixed equatorial button.
3. Persist actual survey knowledge (replay-safe, permission-scoped) alongside existing survey archive; move the stored generator version and envelope under world-genesis authority so a future algorithm change never relocates settled sites.
4. Resolve the older `survey_region`/`survey_landscape` compatibility outputs and migrate every remaining game-facing planet-level consumer to one shared world projection. Do not present this subset as full retirement of legacy pathways.
5. Link planetary construction and local material sources to the same physical region only after commit-safe SQLite allocation and source conservation.

**Gate status: live native route implemented, not yet verified, with explicit remaining persistence/complete-consumer migration gates.**
