# Stage 6 — original physical planet / watershed integration (STAGE6-A)

**Status:** source + compiled native acceptance fixture authored, **not yet executed on Windows/Linux**. Draft, stacked on WO5 PR #92; no native gameplay or GPU surface rendering claimed.

## Original authority join

- `Stage6PlanetSurface.hpp` makes one read-only `NativePlanetSurface` from an already-persisted `PersistentWorld` and a **currently authorized** original ship owner.
- `Simulation::celestial_navigation_anchor` establishes the existing exact physical body identity, system, radius and high-precision cosmic position. Only original planets/moons qualify, never a client-created ID, star or debris object.
- `Simulation::scan_planetary_geography` reuses the existing PGF-01B.2 authoritative watershed, drainage and elevation for an original body with exact 36×72 cell identity and generator revision.
- The tile binds the original journal generation, tick and state hash. A new original event/transaction invalidates the view; it never implies a landing, resource spawn, ship teleport, cargo reservation or NPC relocation.
- Source `tests/STAGE6_OriginalPlanetSurfaceTests.cpp` creates a real original persistent galaxy with starter scanner and finite matter, refuses a foreign owner/fake planet/invalid coordinate, compares repeated survey identity, invalidates a stale view after a real game tick, then copies/recovers genuine SQLite and verifies the same elevation/drainage, body radius and material stock. Registered as `galaxy_stage6_original_planet_surface`.

## Still missing

- **STAGE6-B:** finite source-backed planetary genesis envelope with persistent generator revision, orbit/axial tilt, water/ice and climate conservation; never derive real stock from render metadata.
- **STAGE6-C:** high/low-detail streaming surface tiles with stable 3D floating-origin mapping, longitude seam and poles across worker migration; physically test in continuous galaxy.
- **STAGE6-D:** real Godot planetary viewport tied to exact original survey/host receipts, progressive terrain mesh and collision, stable camera/LODs, no independent Godot planet.
- **STAGE6-E:** original landed shuttle/pilot and return path and cargo integrity; only existing WO3/WO5 original operations can authorize landing/settlement.
- **STAGE6-F:** one exact-head native Windows/Linux CTest gate plus installed Windows Radeon AI Pro R9700 frame pacing, save/restart parity and visual inspection.

This work **does not bypass WO3/WO4/WO5**; it is deliberately downstream and remains draft until the physical logistics stack is accepted.

## STAGE6-B/C native implementation update (2026-11-10)

- `Stage6PlanetTiles.hpp` derives **bounded, non-authoritative 3D tile vertices and triangle indices** exclusively from `observe_planet_surface` and current `scan_planetary_geography` receipts, retaining body/system identity, source elevation and full world state hash. 5 × 12 latitude/longitude tile bands, maximum 4 tiles per request and exact adjacent edges; the longitude wrap uses one same source cell at 72 ≡ 0 rather than adding an independent fake planet. This is a CPU-side mesh description; no Godot planet viewport has been executed.
- `STAGE6_OriginalPlanetSurfaceTests.cpp` now tests the 3D mesh vertex and adjacency/seam positions at full/coarse LOD, original owner and body denial, invalid and repeated tile bounds, stale ticks, and independent SQLite-copy vertex parity.
- `Stage6AuthorizedLanding.hpp` is a strict **read-only** original-shuttle landing receipt join: actual surface location, current original owner/source ship, physically landed shuttle phase 2, original recorded descent ID, nonmoving flight request, and named original pilot NPC currently at the real surface. It **does not** authorize player landing, camera relocation or any cargo creation.
- `STAGE6_AuthorizedLandingTests.cpp` uses real mined/refined modules, original named construction crew, actual planet transit/site, original shuttle pilot, three native descent ticks and receipt, copied SQLite restart, physical ascent and one-use refusal.
- Exact-head native Linux/Windows Stage 6 acceptance is in the opt-in original work-order matrix (with WO2/WO3/WO4/WO5); it is **not accepted until the jobs truly compile and run**.
- Actual real-time native 3D Godot visualization, streamed GPU LOD/collision, physical Radeon R9700 frame timing, full climate and planet genesis, and separately authorized player landing are **still not implemented/accepted**. Do not claim a player travelled to a surface from these test receipts.
