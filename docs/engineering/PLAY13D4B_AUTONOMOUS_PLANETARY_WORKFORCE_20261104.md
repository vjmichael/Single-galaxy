# PLAY-13D.4B — Autonomous Planetary Workforce Operations

**Implementation checkpoint:** native C++ worker scheduler, v79 canonical SQLite state, host/HTTP/browser orbital policy administration, and cross-platform test additions committed to draft PR #48. **Exact-head Windows/Linux native acceptance pending.** No player avatar, planet rendering, or untracked fictional NPCs.

## Gameplay contract

After PGF-01D.4 NPC-delegated spaceport construction completes, the player can administer a work policy at the exact physical region key (body, latitude, longitude, generator revision). Policy is independent of ship position. The owner may update policy remotely; only physical cargo transfers require the ship at the correct celestial frame.

Modes: `0=PAUSE`, `1=FARM`, `2=EXTRACT_WATER`, `3=EXTRACT_NUTRIENTS`, `4=MAINTENANCE`.

- A policy order requires an **operational spaceport owned by the caller**; `FARM` requires installed local farm equipment, `EXTRACT_*` requires installed extractor equipment and a real finite oasis reserve at the physical body. An orphaned v77 legacy site without assigned named NPCs cannot begin autonomous work.
- The simulation checks the **same two named deployed NPCs** at the non-rendered surface custody location and their existing medical duty eligibility every step. If absent, dead, injured or in transit, production does not occur. Finite food, water and oxygen support checks run before work.
- Work executes every 20 committed world ticks; time is game ticks, not continuous background threads. **No player action or polling is needed** while the authoritative world continues stepping.
- Farm policy: one batch `10kg local water + 5kg nutrients -> 12kg food + 3kg wastewater` per eligible work step. This reuses the established regional recipe and material ledger, and increases the existing `harvest_cycles` count.
- Water/nutrient extraction policy: up to 1000kg per eligible work step, **debited** from the existing finite planetary oasis reserve and credited to the same regional installation's stock. The extractor only operates at its previously verified unique resource region, because it could only be installed there.
- Maintenance: when equipment condition is below 100%, one kilogram of local nutrients is debited and one kilogram of waste is produced, improving condition up to 10 points (capped at 100%). Farming and extraction wear equipment by 1 condition point every five successful non-maintenance cycles. At zero condition, productive policies stall. **This is an intentionally simplified repair-material recipe**, not a physically complete engineering process.
- Worker food, water and oxygen are continually consumed by the existing surface supervisor (2kg each every 10 ticks for two workers); they become equal mass in organic waste, wastewater and CO₂. No free life support is generated.
- Every successful autonomous work cycle increments `autonomous_cycles`. Attempted work without materials, personnel or sound equipment increments `stalled_cycles`, and does not spend nonexistent supplies. `last_work_tick`, extraction kilograms, repair cycles and equipment condition are persisted.
- Policy `PAUSE` prevents further production. Resident NPCs still require life support while physically assigned on the surface; administrative pause is *not* magical evacuation.

## Authoritative state and recovery

- All work mutates `Simulation::planetary_sites_` and existing `oasis_reserves_`, not a new side ledger. Work executes inside the normal committed `Simulation::step` and `PersistentWorld` snapshot path.
- The full world `total_ore` ledger counts both ship and regional stocks, including water, nutrients, food, oxygen, CO₂ and waste. Each conversion and reserve transfer must preserve world material total.
- `galaxy-kernel-v79` adds exactly seven fields to v78 site records: policy, condition, last work tick, successful cycles, stalled cycles, extracted kilograms, repair cycles. Parser bounds all fields, checks policy/equipment coherence and physical counters. Older v78 sites default to paused, good equipment, zero production counters.
- Set-policy writes an isolated candidate world, validates source-conserved mass, commits via SQLite journal and publishes only after successful commit; failure injection must leave published state unchanged.
- Policy administration is allowed remotely after a ship leaves orbit, but physical resupply/retrieval still uses the original vessel/body frame validation. Autonomous work uses local stock only and can continue until finite stocks run out.

## Orbital command and UI

- `PLANET_POLICY|body|lat|lon|mode` host command maps through strict `colonyPlanetPolicy` relay validation. Native receipt `PLANET_POLICY_COMMITTED` includes coordinates, policy and committed generation.
- Existing `PLANET_SITE_STATUS` telemetry appends policy, condition, work cycles, stalled cycles, extracted kilograms and repairs. The browser shows operational status and a dropdown to change NPC work policy. There is no surface renderer.
- Existing player-authored physical transfer commands are **not** replaced by automated long-distance cargo teleportation.

## Acceptance added

- Extended `PLAY13D_RegionalFacilitiesTests.cpp`: real manufactured construction, explicit NPC custody, commissioning, farm or extractor equipment, fail-injected policy rollback, 100 autonomous simulation ticks with finite production, world mass preservation, maintenance using local inputs, pause without new production, remote policy switching after ship departure, lack of remote supply, v79 canonical round-trip and SQLite restart.
- `PLAY13D_PlanetSurveyNodeTests.js`: valid and malformed native policy and workforce telemetry.
- `PLAY13D_PlanetSurveyRelayTests.py`: authorization and strict JSON policy typing/range in real HTTP-to-native relay.
- Existing Windows/Linux `play13d.yml` runs these test families. Do not claim a passing native gate unless **exact-head** compiler and runtime tests succeed.

## Open next gates

1. Real Windows/Linux compilation and fail triage for PGF-01D.3/.4/.4B, plus native end-to-end gameplay command harness.
2. A persistent **resource-backed NPC shuttle** and explicit surface/orbit transfer capacity. This first gate still models an abstract delivery when original ship is in the same celestial frame; it does **not** model a shuttle vehicle or hard landing casualty.
3. Durable replenishment and staffing requests, rather than teleporting supplies or recruiting free NPCs. Autonomous logistics may detect shortages, but NPCs cannot receive anything unless physically delivered via authorized cargo operations.
4. Real environmental engineering: pumps, energy budgets, temperatures, radiation, pressure, weather, power failures, orbital lifting, building condition and actual biome-dependent farming.
5. NPC skills/traits/morale/job experience and exact crew replacement, recall, evacuation and dependent custody.
6. Robust idempotent request IDs for repeating material operations and policy commands. Existing persistence guarantees atomic state updates but not exactly-once external request delivery.
7. Explicit biome life/non-life and geological genesis with region-located independent finite sources (the current oasis stock is body-level with one unique surface source waypoint).

**Non-goals:** no playable planetary avatar, surface tile rendering, full environment streaming or immediate spaceport completion.
