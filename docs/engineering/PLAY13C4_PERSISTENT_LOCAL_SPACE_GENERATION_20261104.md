# PLAY-13C.4 — Persistent Physical Local-Space Generation

## Implemented checkpoint: finite body-region deposits (v73)

Each real catalogued celestial body can now have a deterministic, physically persistent local region. The world creates it only when a journaled ship transit to that body actually completes (or, during startup migration, when a pre-v73 save already contains a ship anchored there). This is not merely browser decoration.

- `Simulation::seed_celestial_local_region(body_id)` validates the genuine v70 celestial catalogue ID and refuses to invent mass. If an eligible, unanchored asteroid in the **same star system** has at least 15 tonnes available and is not being actively mined, the generator moves **14 existing tonnes** out of it into two local physical asteroids (6 t and 8 t), positioned 2 m and approximately 13 m from local arrival at bounded native coordinates. The donor retains at least 1 t. Allocation is deterministic by ascending donor ID; it does not use random number generation, free goods, NPCs or market credits. If no suitable existing reserve exists, the body is persistently recorded as a barren region (no resource creation).
- New rocks are full `EntityType::Asteroid` entities with persistent IDs, positions and quantities, not simulated map-only signatures. Their `local_entity_anchors_` associates them with exactly one physical body, making them invisible and unmineable in the old frame.
- v73 canonical snapshot adds `localregion|body|first_site|second_site` (or `0|0` for barren) and `localanchor|entity|body` records. The immutable site manifest allows restore to establish that each allocated site remains as a correctly anchored physical asteroid **or** has a corresponding durable depletion tombstone. Duplicate sites, nonexistent bodies, wrong system, orphaned anchors, and missing sites are rejected. All values remain covered by the existing SQLite world snapshot hashing and ore-origin conservation validation.
- `Simulation::same_local_frame` now checks both ship frame custody and rock anchors. A completed arrival updates the ship and seeds the body **in the same SQLite transaction**. Failure injection leaves the original unarrived ship, donor ore and absence of the region intact.
- The existing actual C++ `SurveyLocal`, `Mine`, `StartMining`, `StopMining`, and autonomous mining reach these asteroids only within their matching local frame and normal distance/resource rules. Anchored ships remain conservatively blocked from unrelated unaudited legacy commands; `Thrust` is still allowed. Local signature scans and owner-specific projections now include the generated region and exclude the source donor.
- `PersistentWorld::ensure_visited_local_regions()` is an explicit journaled, idempotent startup upgrade for older persistent ships which arrived under v72 before generation existed. Future first arrivals seed automatically.
- The existing host scan after arrival emits native `ROCK`/`SMALL_ROCK` messages. The browser already invalidates stale old-region targets; native player commands operate on original persistent entity IDs.

## Regression and CI coverage added

`tests/PLAY13C_LocalRegionGenerationTests.cpp` drives arrival, SQLite failure rollback, v73 snapshot restore, signed real mass conservation, visible local signatures, authentic native survey, actual ore extraction and despawn tombstones, tampered anchor rejection, recovered copies, travel to a second body and return without respawning depleted ore, and a genuinely barren no-resource world.

Updated `PLAY13C_FrameVisibilityTests.cpp` asserts that the source donor and ships remain hidden while two local deposits appear in both delta and restart snapshot. Updated Linux live-host smoke requires v73 persistent region records. PLAY-13C Windows/Linux native workflow now includes `galaxy_play13c_local_region_tests`.

## Scope restrictions / open gates

This is a **finite-resource local asteroid prototype**, NOT full procedural geology or a visually rendered planetary surface. The first stock is reallocated from an existing unanchored body as a conservative migration/bootstrap mechanism; a production galaxy should allocate geological reserves to physical regions at galaxy genesis rather than relocating ore on discovery. Deposits are currently simple ore asteroids, not yet differentiated taxonomic minerals or physical planetary terrain.

There is no new asteroid respawn, resource replenishment, free replacement, inflation, gravity, atmospheric physics or independent collision model. Native local survey radius is currently 50,000 mm (**50 meters**) and mining range is 5,000 mm (**5 meters**), so this first prototype places sites within those existing ranges; these are not gameplay-balanced interplanetary distances. Existing host combat, docking, local facilities and NPC automation need frame-by-frame validation before re-enablement.

**Verification pending:** current-head actual Windows/Linux C++ compile, all CTest native regressions and real live-host SQLite restart acceptance must pass. Do not treat a queued GitHub job as verified or mark PR #47 ready for merge.

Next: production reserve allocation, regional 3-D scene mesh/rendering and physical geology, granular audited anchored actions and multi-ship frame-aware command/event privacy.
