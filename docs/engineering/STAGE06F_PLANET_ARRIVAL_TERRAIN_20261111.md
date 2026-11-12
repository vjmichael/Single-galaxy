# S6-F — Actual Planet Arrival & Playable Terrain

**2026-11-11 | branch:** `sg/stage6f-original-planet-arrival-terrain-20261111`
**Status:** original-world terrain/NPC-shuttle-observation and local Godot inspection **SOURCE STAGED**, not a completed player-landing feature. Native C++/CTest, Godot headless, Windows/Linux installed and physical Radeon AI Pro R9700 acceptance are **NOT RUN**. No PR merge, release, or main-branch mutation authorized.

## Source ownership and recovered precursor

This work is stacked on `sg/stage6d-physical-environment-20261111` at `06ce14a` (S6-C v127, S6-D v128/v129, S6-E v130). It recovers 11 previously authored source files from the still-unmerged `stage6/planetary-galaxy-projection-20261110` (`8cccf7a`) branch. Recovery is a **file-level adoption**, not a merge or a claim that those prior branches passed Windows/Linux or physical gameplay tests.

The only terrain authority is `Simulation::celestial_navigation_anchor`, `scan_planetary_geography`, its canonical PGF-01B watershed and the original `PersistentWorld` state hash/generation/tick. Planet identity, ship/scanner authorization, geographic cell revision, and read-only surface observations remain pinned to the original world. **Never** turn a display mesh into a planetary resource producer, an original player position, or a second planet registry.

## S6-F source work

1. **F1 — Real original planetary approach observations (source staged).** `Stage6PlanetSurface.hpp` binds every returned terrain observation to an original owned scanner-equipped ship, exact planet/moon ID, original physical radius/anchor, 36×72 original source geography, original tick/generation/hash and PGF revision. Invalid ship, owner, body, revision or region are refused.
2. **F2 — Progressive visual terrain (source staged).** `Stage6PlanetTiles.hpp` produces 5 latitude bands × 12 longitude bands at full/coarse stride (1 or 2) with shared native sample vertices across adjacent tiles and the 72→0 longitude seam. At most 4 tiles per native export; Godot keeps a visual cache of at most 12 tiles. Native geometry is a unit-sphere visual mesh, **not** the true physical radius or authoritative collision/landing.
3. **F3 — Real NPC shuttle arrival observation (source staged).** `Stage6AuthorizedLanding.hpp` gates on the original constructed site and spaceport, completed native descent receipt, exact source ship/owner, idle landed shuttle, and named original pilot NPC at the original surface location. Requests/in-flight phases and ascent revoke that observation. This was authored in the older Stage6 branch and is now carried into the v130 source stack.
4. **F3A — Source-safe desktop mesh bridge (new source).** `galaxy_stage6_godot_export` is registered in CMake alongside the two original terrain/shuttle tests. `--landed=LAT,LON,1` requires the real `NativeLandingObservation`; without explicit tile addresses it chooses the actual landed region. The tool exports from a verified `PersistentWorld::recover_copy` temp SQLite snapshot, never opening the original save through its render read-model. Destination aliases of the original SQLite file and WAL/SHM/journal are refused, and output is written through a temporary file plus atomic rename.
5. **F3B — Godot mesh/collider inspection (new source).** `SG-STAGE6-GODOT-01` packets include exact original string identity, real NPC descent receipt, pilot NPC and physical surface-location ID; malformed/foreign/in-flight descent is refused in C++. The Godot 4 scene renders native tiles as ArrayMesh with corresponding ConcavePolygonShape3D, validates a full incoming batch before publishing, and clears stale caches on original generation/hash changes. After a currently valid **NPC shuttle** landing packet containing its region tile, a local `SurfaceInspector.gd` scene allows WASD/mouse spherical-gravity traversal with Godot collision; **the inspector is explicitly NOT the original player**. It neither sends a command nor records a durable position.
6. **F3C — Staged test evidence.** `STAGE6_OriginalPlanetSurfaceTests.cpp` asserts ship/owner/body denial, exact native mesh seam, LOD edges, source hash immutability and SQLite copy parity. `STAGE6_AuthorizedLandingTests.cpp` asserts real constructed, fueled, piloted original shuttle descent and return, exact descent receipt, original surface custody, source material conservation, read-only Godot packet contents and denial on launch/ascent. These tests are **registered but not executed** on the current branch.

## Not yet implemented / must remain open

- **F4 — Actual player descent and re-embarkation.** A named original player-person requires explicit transport admission (not an NPC pilot receipt), a real on-board seat/manifest, fuel/pressure/approach and destination checks, legal/site authority, exact one-use descent and ascent receipts, durable `PlayerPersonState` surface identity and source-compatible SQLite migration. No Godot terrain collision or camera position may grant player transport. The current `PERSON/POSE` station/pilot host remains authoritative and cannot yet express persistent planetary walking.
- **F5 — Native surface locomotion and actual NPC traversal.** Continuous planet-relative 3D frame for person/NPC, collision/contact server checks derived from original physical planetary geometry, environment/life-support effects, movement acknowledgements, recoverable worker custody, and return to the same original shuttle are pending. `SurfaceInspector` is an isolated **local preview**, not F5 completion.
- **F6 — Consolidated desktop integration and LOD scale.** Connect source-pinned live native terrain packets into the installed `desktop_godot` game, not just the isolated `godot/stage6` sample; add continuous paging under 3D floating origin, polar cap handling and real rendered-planet approach; bounded collision-cache eviction, retry/resync, and multi-body performance need tests.
- **S6-D5 / S6-E extended backlog.** Cross-region geological water/soil/runoff transport, soil/mineral/volatile onward freight, real equip-and-extract installed acceptance and extended biosphere/industrial integration remain separately OPEN. A survey/renderer must never claim original regional kg as cargo.
- **S6-G acceptance.** Exact-head Linux/Windows compiler, CTest, native host, original v86–v130 SQLite recovery and tamper checks; owner/scanner denials, original mass parity, 4-cell/long-run simulation, installed Godot and actual physical Radeon AI Pro R9700 latency/frame pacing and player-input acceptance. No scripted CI invocation or PR acceptance has been undertaken.

## Source-driven developer inspection (not installed game acceptance)

After building the `galaxy_stage6_godot_export` target, obtain the *real* owner, scanner ship and body ID from an original saved world. For read-only orbital terrain:

```text
galaxy_stage6_godot_export ORIGINAL.sqlite OWNER SHIP BODY stage6.json 0,0,1 0,1,1
godot --path godot/stage6 -- --packet stage6.json
```

For inspection at an **already committed original NPC-piloted shuttle landing**:

```text
galaxy_stage6_godot_export ORIGINAL.sqlite OWNER SHIP BODY stage6.json --landed=LAT,LON,1
godot --path godot/stage6 -- --packet stage6.json
```

The native exporter must reject that second request without a currently landed original shuttle and exact pilot/ship/site custody. Even after acceptance, these commands do not transport a player. `godot/stage6` is an engineering visual proof, not a second game launcher, save writer, or user-accepted installed package.
