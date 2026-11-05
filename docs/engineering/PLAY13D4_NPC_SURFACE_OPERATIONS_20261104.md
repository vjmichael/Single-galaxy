# PLAY-13D.4 — Orbital NPC Delegation and Planetary Spaceports

**Status:** implemented as a version-78 canonical-world candidate on stacked draft PR #48. Native Windows/Linux acceptance **pending**. No full/tiled surface rendering. This gate deliberately keeps the player and their controllable ship in the celestial orbital/local frame.

## Governing design
- The player **never controls a walking surface avatar or descends to terrain**. Planetary development uses orbital/ship commands and telemetry, analogous to Eve-style management.
- Player surveys a real planetary cell, physically arrives in the celestial frame, fabricates modules, selects two **actual named NPCs** from the existing finite ship roster, and issues a construction order. The host can auto-select two *eligible named* personnel if the user omits IDs. No NPC creation from survey, construction, or automation.
- A durable surface custody handle is deterministically derived from `(body,lat,lon,revision)`. It is not a traversable map entity, not a player landing location, and never appears as a world-space ship.
- Two workers leave the ship's actual crew count and have `NpcRole::PlanetarySurface`, `location` and `assigned_site` recorded at the non-rendered surface custody location. Injured/unavailable personnel cannot be chosen; the host also refuses to detach a registered dependent's guardian.
- The player retains administrative ownership of the outpost. Persistent records include first/second NPC ID, original ship, physical body, exact coordinate, phase, progress, support consumption, resources, installed modules and eventual operational flag.
- `PLANET_CREW` lists currently eligible named NPCs. `PLANET_BUILD` optionally takes two IDs. `PLANET_SITE_STATUS` returns current progress without rendering terrain or requiring the ship to stay in orbit, but is owner-only.
- Abstract 4 committed ticks of personnel/equipment deployment precede 240 committed ticks of NPC spaceport construction. At 20ms/tick, 240 ticks is about 4.8 seconds; at slower game clocks, it takes proportionally longer. Times are in committed **simulation ticks**, not real astronomical minutes.
- Construction starts with **two real manufactured modules** and finite 60kg each food, water and oxygen debited from the actual ship. Crew metabolizes supplies every 10 ticks: 2kg food -> organic waste, 2kg water -> wastewater, 2kg oxygen -> CO2. All remain in the same mass ledger. Sick or missing workers and exhausted support pause construction without free progress.
- Spaceport phase 1 = NPC deployment; phase 2 = under construction; phase 3 = operational. A farm/extractor may **not be installed** while building. Region-specific farm suitability and unique finite oasis source coordinates still gate equipment and production; no biome labels produce kilograms.
- Surviving staff remain assigned to the surface after the spaceport becomes operational. Support consumption continues while workers remain there. Orbital resupply commands move finite food/oxygen from the ship to site; existing site water supply handles worker water; all use `PersistentWorld` atomic world journal.
- The player can leave orbit after dispatch; site construction ticks and support use continue in the persistent world. No automatic recall, labor replacement, public transport service, planetary shuttle asset or surface vehicle is implied.

## Durable contract
- `Simulation::planetary_sites_` remains the single world-authoritative site registry; no second independently ticking NPC planet database.
- Canonical `galaxy-kernel-v78` extends v77 site rows with phase/progress, surface custody handle, exact NPC IDs, worker count and the additional oxygen/carbon/waste stocks. Strict parser validates supported phases, construction cap, nonnegative physical stocks, manifest uniqueness, source bodies and eligible NPC assignments.
- Named NPC snapshot validation recognizes `NpcRole::PlanetarySurface=3` **only in v78 or newer**, verifies each named worker belongs to its original ship and site, and reconciles ship and surface headcounts. Roles are not cloned across inventories.
- v77 completed facilities remain usable as grandfathered sites with zero retroactively assigned staff; they do not acquire invented NPCs. New projects require real named NPC records.
- `PersistentWorld::build_planetary_site` retains transactional snapshot failure injection; changes publish only after SQLite commit and `total_ore()` still equals original material endowment. Worker food/water/oxygen and waste stocks are counted exactly once.
- Browser UI exposes a collapsed text-only region command panel, two optional NPC ID inputs, roster query, stage progress and support quantities. No ground renderer, 3-D terrain section or on-foot controls are added.

## Tests
- Existing `PLAY13D_RegionalFacilitiesTests.cpp` now enables the genuine finite named NPC registry, selects two explicit IDs, denies duplicate/unknown selections, verifies both NPCs move to an exact surface custody handle, ship crew decreases, construction does not finish instantly, reaches phase 3 after 4+240 committed ticks, conserves ore-equivalent material mass, supports finite supply, survives SQLite restart and remains on the planet when the ship leaves.
- Node telemetry acceptance checks NPC roster, malformation denial, phase state and source/operation protocol.
- HTTP relay acceptance checks optional explicit NPC pairs, valid status and support requests, strict field rejection, boolean/overflow denial and authorization.
- Exact-head Windows/Linux native CI and real host end-to-end acceptance are still needed; do not claim the new code production-verified until all checks pass.

## Important deferred work
1. **Physical uncrewed ferry or NPC shuttle transfer/capacity**: the first 4 ticks are a simplified sealed transport/equipment delivery stage; no independent shuttle asset or crash-casualty mechanics yet.
2. **NPC replacement / recall / evacuation**: an injured or lost assigned NPC pauses work; later commands must dispatch a replacement without duplicating workers, and enable return through a supervised transport transaction.
3. **Project modifiers**: hazard gear, radiation, geothermal and atmospheric hazards, pressure/temp-dependent life support, weather interruptions, repair, workers' skill and morale, power requirements, building condition, construction cost by geography.
4. **Spaceport logistics**: port can be operational but is not yet a physical landing/docking service, autonomous cargo elevator, NPC traffic simulation or market.
5. **Independent per-region source inventories and delayed scheduled farm/extraction orders**: finite body reserve remains located by one deterministic regional waypoint. Surface farm/extractor work still needs actual delegated production schedules and an authorized physical logistics interface.
6. **Exactly-once request identity and query knowledge**: command validity/duplication protections exist for construction but repeated valid material operations are not protected by an independent unique request-ID receipt.
7. **Real end-to-end native validation** at latest head and finite-population/accounting regressions across old snapshots.

The gameplay target is **orbital management and NPC-driven infrastructure** — a rich planetary simulation beneath the interface, without requiring planetary graphics.
