# S6-C — Immutable Planetary Genesis & Finite Regional Resources

**2026-11-11. Development branch:** `sg/group4-dock-colony-inventory-run-20261111`. **Status:** source integration completed provisionally; genuine Windows/Linux native and Godot installed acceptance OPEN. No CI, CTest, native build, user-operated Radeon install, PR merge or release claimed.

## Original authority preserved

S6-C extends the EXISTING `Simulation` / `PersistentWorld` and its ORIGINAL SQLite canonical snapshot and material-origin proof. There is **no second world generator, no second cargo ledger, no runtime-created free matter and no alternate planetary body ID**. Previously native-authored `PhysicalCelestialBody`, `PlanetaryWorldKey`, original PGF v1 `PlanetaryEnvelope` and `PlanetaryWatershed` remain source authorities. The earlier `PhysicalOasisReserve` and `OasisExtractor` remain independently funded and unchanged by historical migration.

### S6-C1 — Immutable physical body identity and environment (source implemented)

`Simulation::ensure_planetary_genesis_manifest()` pins a v127 record **for each existing original physical planet or moon**, including uninhabited bodies. Each `PlanetaryGenesisBody` holds the native galaxy seed, system, body ID, generator revision 1 and the exact derived climate envelope (climate, surface-water coverage, mean temperature, independent biosphere flag). Existing bodies carry **ZERO new kg by default**. Repeated migration cannot alter the envelope, existing quantities, or original astro IDs.

Snapshot records `planetgenesis` and `planetresource` live INSIDE `Simulation::canonical_state`, not a separate SQLite database or JSON read-model. `galaxy-kernel-v127` is additive; prior snapshot versions remain readable. Every restored body record is checked against the actual recovered celestial catalogue, climate-envelope derivation and generator revision. Missing, orphaned, altered, duplicated or mismatched records are rejected.

### S6-C2 — Finite funded original body and region resources (source implemented)

Four typed source-stock classes, stored in **integer kilograms** with whole-tonne seed units:

| Class | Body-level bounded original stock | Regional physical stock |
| --- | --- | --- |
| Water | `original_kg[0]` / `unallocated_kg[0]` | `stock_kg[0]` |
| Soil | `original_kg[1]` / `unallocated_kg[1]` | `stock_kg[1]` |
| Mineral | `original_kg[2]` / `unallocated_kg[2]` | `stock_kg[2]` |
| Volatile | `original_kg[3]` / `unallocated_kg[3]` | `stock_kg[3]` |

`provision_planetary_body_genesis` and `place_planetary_region_genesis` operate only at original **tick zero**, before any command or journal submission. Only a real physical planet/moon can be funded. Each type must be nonnegative, at most 100,000,000 kg and an exact multiple of 1000 kg. Explicit provision is once per body. Region placement is **unique per exact (body, latitude, longitude, generator revision)** and debits that body's unallocated kilograms. Allocation is candidate-and-swap all-or-nothing; impossible ocean-bed soil/mineral placements and water allocations on zero-water bodies are refused. No client can nominate arbitrary source mass after genesis.

The unchanged original **`total_ore()` material-origin verifier** includes all current unallocated and regional stock exactly once, alongside existing ships, oasis reserves, planetary facilities, cargo and other inventories. Restores enforce for every category:

`original_kg == unallocated_kg + sum(all physically allocated region stock_kg)`

These source stocks are **unmined, uncollected physical source**, not cargo, food, fertilizer, company inventory or owner property. They can only be moved by a future separately authorized and conserved extraction/transport implementation.

### S6-C3 — Historical SQLite migration (source implemented)

`PersistentWorld::ensure_planetary_genesis_manifest(FailPoint)` uses the ORIGINAL `Store::commit` transaction, exact original generation/hash, candidate world and failure rollback. For a historical v126 or older save, migration creates **only zero-stock native planet envelopes**; it never regenerates existing asteroids, reassigns finite `OasisReserve` values, spawns NPCs, or reallocates material. It checks original `resource_origin_` and rejects any changed total. Repeat migration is idempotent. `AfterSnapshotWrite` fault regression is staged.

### S6-C4 — Both native new-game hosts and survey UI (source implemented)

`PlanetaryGenesisBootstrap.hpp` is a single shared *new-world-only* helper used by the existing native colony host and installed desktop station writer. It registers all original physical planetary envelopes with zero material, then finds the first eligible **non-ocean land region on a world with water potential**, and provisions exactly **38 tonnes** of finite original body geology: 6,000 kg water, 12,000 kg soil, 16,000 kg minerals, 4,000 kg volatiles. This is planet-local **geological stock**, not a gift aboard the player ship or an assembled factory. If no qualifying body exists, no source is fabricated.

In both native hosts, existing SQLite saves ignore the new-world seed, retain their original cargo/oasis/settlement, and go through a separate zero-material `PersistentWorld` migration. The original authenticated `PLANET_SURVEY` now reports an additional `PLANET_SOURCE` only when there is an actual finite region stock. Its owner, fitted scanner, body and coordinates must already be valid. `viewer/colony.js` strictly decodes the four kg source amounts, body/revision and bounds, joins to the same native survey tick and explicitly denies that observation provides extraction permission.

The actual Godot player view does not yet supply full planetary landing or body survey; the installed station host now contains native genesis custody but the player-facing surface arrival and resource extraction are distinct later development groups.

### S6-C5 — Staged regression and security assertions

- `tests/Stage06FinitePlanetaryGenesisTests.cpp`, registered as `galaxy_stage06_finite_planet_genesis` in CMake: original planet discovery and finite seed, two disjoint land-region placements, conserved 20-tonne provision, owner/scanner denial, out-of-range/duplicate/overallocated source refusal, missing-body refusal, no mutation after world starts, canonical v127 snapshot reparse, region-stock tamper refusal, SQLite restart, rollback injection and historical zero-material migration.
- `tests/PLAY13D_PlanetSurveyNodeTests.js`: new `PLANET_SOURCE` strict field-count, canonical quantity/ID/revision, overflow, negative, zero-row, wrong-location refusal and native/UI wiring checks.
- STAGE6-ENV01 seasonal and older PGF geography tests remain staged for consolidated acceptance; none have been independently executed at this source head.

### Acceptance still OPEN

1. Compile complete original C++ kernel, station and colony native hosts on Windows/Linux and execute new `galaxy_stage06_finite_planet_genesis` + PGF/WO3/WO4/DOCK regressions. Fix actual errors without weakening original custody checks.
2. Genuine historical v86/v126 SQLite upgrade, original resource-origin parity, injected failure/corruption, deterministic recovered copies and versioned `SurveyArchive` history.
3. Installed Godot game with real player-controlled orbital survey and surface navigation, source-pinned discovery and owner rejection. No owner physical Radeon gameplay acceptance has been recorded for these changes.
4. Long-duration multi-system/prospective-planet performance and bounded query/readback under original supervisor.
5. No **consumption** of S6-C regional sources is claimed. Soil, mineral, volatile and water stock release requires separately specified native equipment, facility, skill, energy, legal/owner custody, real transport and irreversible conservation receipts. Do not convert existing `OasisReserve` water into a second duplicate source.

### Next Stage 6 work

**S6-D — Physical Environmental Processes:** connect finite original geological resource budgets to credible seasonal water/ice and soil conditions, eventually admitting source-preserving and durable authorized extraction; model resource transfer through the original inventory only. **S6-E** then attaches ecology, actual crops, labor and industrial needs to those physical sources; **S6-F** handles actual surface terrain and player arrival.

**Merge/release to main:** not authorized. **S6-C source provisionally complete, acceptance OPEN.**
