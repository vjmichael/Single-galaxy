# PLAY-13C.3 — Frame-local simulation views and native gameplay boundaries

## Implemented on stacked draft PR #47
- Native `ClientContract::project(world,owner)` now uses the viewer's actual owned ship as the current local-frame observer. Objects must match BOTH star system and `Simulation::same_local_frame`; remote-body ships, asteroids, stations and their projected storage, markets, docking and mining state are excluded. Legacy `owner=0` public-test behavior remains unchanged.
- Existing `ServerViewBridge` snapshot/delta reconciliation removes entities when the authenticated observer changes celestial frame. Local physical activity is emitted only if an actor/target was present in the previous or current view. Player wallet remains owner-private.
- Native `Simulation::coarse_probe_signatures` and `mine_status` now check local frame; a scanner cannot show an old-frame ore body merely because its coordinates numerically overlap.
- `colony_live_host.cpp` local asteroid scans and nearest-target choice are frame-scoped. `LOCAL_FRAME|system|anchor` identifies the authoritative local region; after a committed `FRAME_TRANSIT_ARRIVED`, local status and scan refresh.
- Browser `viewer/colony.js` parses strict `LOCAL_FRAME`, `FRAME_TRANSIT_STARTED`, `FRAME_TRANSIT_ARRIVED` telemetry and offers `Travel to local region` controls for server-surveyed celestial bodies. Travel submits only the body ID via the authenticated relay action `colonyFrameTravel`. On arrival stale asteroid/signature selections are cleared. No client-supplied coordinates are accepted.
- Native test `PLAY13C_FrameVisibilityTests.cpp` drives an actual committed transit through `PersistentWorld`, exercises snapshots and deltas with `ClientView`, verifies off-frame ships/asteroids and signatures are invisible, and checks exact restart isolation. Native Windows/Linux CI compiles this and previous transit tests.

## Important limitations
- These are 2-D local simulation instances attached to 3-D astronomical origins. There is no planet mesh, surface terrain, gravity, procedural local asteroid seeding or real-time flight across astronomical distances. An arrival region may legitimately be empty.
- The current ordinary action executor still uses a conservative rule: a ship anchored to a nonzero body cannot perform most legacy actions except thrust. Frame-specific gameplay actions should be re-enabled individually only after their targets, automated effects, and associated NPC/site authority have been audited end to end.
- Command-result event records contain IDs but no authoritative actor/issuer field. While activity events have now been visibility-filtered, the legacy command-result stream is not fully owner/frame isolated. Resolving this requires a durable command-to-owner mapping and correctly scoped receipt projection; do not claim comprehensive multiplayer event privacy.
- Single-ship observer selection for owner projections remains the legacy first-owned-ship rule. Multiple independently remote owned ships need explicit owner-authorized camera/active-ship selection.
- Existing non-spatial global/world queries, NPC scheduling, survey index and future proximity operations must be reviewed before unrestricted gameplay at a new body. Browser rendering shows celestial catalogue and existing scene projection, not physical planetary visuals.
- This remains draft. GitHub native cross-platform test jobs and live-host acceptance must pass on the exact HEAD before marking PLAY-13C.3 validated.

## Next gates
1. Real Windows/Linux C++ build, CTest, browser decoding and the actual host travel/restart smoke, including projection-after-arrival.
2. Make command-result events owner/frame-safe without losing user-visible action outcomes; add active-ship selection.
3. Create geographically anchored stations, asteroids and survey sites through conservative, resource-checked world generation; safe local collision/mining/docking then become meaningful at a visited world.
4. Add 3-D navigational map and local-scene rendering with explicit coordinate transforms, including place labels and a clear remote vs current body distinction.
