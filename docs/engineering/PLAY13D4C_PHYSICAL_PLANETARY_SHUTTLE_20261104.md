# PLAY-13D.4C — Physical Planetary Shuttle and Logistics Operations

**Status:** engineering contract / implementation gate; no native implementation claimed by this document.  
**Parent:** PLAY-13D.4B (v79 autonomous workforce), draft PR #48.  
**Decision:** keep the player in orbit. NPCs perform all surface flight, loading, unloading, and work. No surface avatar, world renderer or surface tile stream.

## 1. Objective

Replace abstract orbit-to-ground transfers with a finite, persistent NPC-operated shuttle. The orbital ship does not land. A shuttle makes real trips between the owning ship's validated celestial frame and the exact persistent planetary installation key: `(body_id, latitude_key, longitude_key, generator_revision)`. The geography seen by surveys, construction, farming and extraction must still be one shared physical identity.

Existing 4B workforce policies and mass ledgers remain authoritative. No new source of food, minerals, crew or equipment is introduced.

## 2. Durable entities

A **ShuttleVehicle** requires:
- unique vehicle id; owner; parent source ship; bound destination site; vehicle construction/fabrication receipt;
- physical dry mass; rated cargo mass; seats; fuel capacity and finite fuel inventory; condition; power/propulsion requirement;
- explicit pilot/crew identities from the finite named NPC roster, with custody location and duty fitness;
- flight state, departure/due tick, remaining trip fuel, exact cargo manifest, destination surface key;
- monotonically increasing command generation and last committed operation receipt.

A **ShuttleManifest** contains cargo in integer kilograms, one of the same enumerated physical material categories already supported by the ship/site inventories. Occupied seats are separate named identities, never anonymous population counts. Manifest mass must be <= vehicle capacity; occupied seats must be <= vehicle seats. A single worker cannot be aboard the source ship, inside a shuttle and on the surface concurrently.

A **ShuttleFlight** has immutable source, destination, request id, assignment id, departure tick, arrival due tick, NPC pilot, and the snapshot of its physically loaded manifest. While in flight, neither endpoint may spend the held cargo.

A minimal launchpad/spaceport is required for normal surface arrival. Initial deployment to an uncommissioned site remains a separate bounded construction bootstrap capability, *not* an unrestricted reusable cargo teleporter.

## 3. State machine

`DOCKED_ORBIT` -> `LOADING` -> `DESCENT` -> `AT_SURFACE` -> `LOADING_ASCENT` -> `ASCENT` -> `DOCKED_ORBIT`.

There are also `BLOCKED`, `DAMAGED`, and `LOST` states; a blocked launch leaves cargo and named NPCs where they physically are, not at the intended destination.

- Load: authorize owner, verify ship and shuttle physical frame, available source inventory, mass/seat limits, named NPC custody and pilot eligibility. Debit source and credit immutable held manifest *atomically*.
- Launch: require fuel, condition, crew, committed manifest, correct surface/body frame and destination readiness; debit trip fuel and record resulting exhaust/residue in the same material accounting boundary rather than deleting conserved matter.
- Arrive: on committed host tick, remove in-flight status and transfer held manifest to destination stock and named NPC custody atomically. If destination refuses capacity, hold cargo in the landed shuttle, not an invisible warehouse.
- Return: identical rules in reverse. Extraction output is transported only after physical loading, launch, transit and unloading.
- Cancel before launch: release the manifest back to its origin atomically. No mid-flight arbitrary cancel/reverse without a distinct recovery operation.
- No worker that is aboard a shuttle can simultaneously contribute to 4B autonomous work cycles.

Flight durations are game-configured ticks, not real-world communication delays. Choose a deterministic bounded travel duration, rather than simulating a realistic multihour atmospheric transfer. Fuel cost, capacity and duration must be visible in the orbital UI before committing.

## 4. Ordering, identity, recovery

All mutations pass native owner authorization and Simulation's normal effect-journal / SQLite commit path. The client never decides arrival or material balance.

- Idempotency key: `(owner_id, request_id, operation)`. Replaying an accepted command returns the same durable receipt without double-debit, duplicate crew or second arrival.
- Simulate on a candidate world, check invariants, journal/commit and only then publish it. Injected SQLite failures must not alter published state.
- Each flight advances once even after restart or duplicate tick processing. Recovery reconstructs the explicit `IN_FLIGHT` manifest and next due tick.
- Validate every endpoint against the **shared PGF-01B.2 geographical region key** and generation revision. Reject stale or mismatched coordinates; never silently remap a shuttle to a nearby biome.
- Lost shuttles, if introduced in later risk gates, move all dry mass and cargo to a durable wreck/recovery ledger and record named NPC fate; they do not delete their mass or silently respawn passengers.

## 5. Command and telemetry proposal

Proposed native commands (not yet implemented):
- `PLANET_SHUTTLE_BUILD|ship|spec`
- `PLANET_SHUTTLE_ASSIGN|vehicle|site_key|pilot_npc`
- `PLANET_SHUTTLE_LOAD|vehicle|direction|manifest|passengers|request_id`
- `PLANET_SHUTTLE_LAUNCH|vehicle|request_id`
- `PLANET_SHUTTLE_STATUS|vehicle`
- `PLANET_SHUTTLE_UNLOAD|vehicle|request_id`
- `PLANET_SHUTTLE_ABORT_DOCKED|vehicle|request_id`

Use native structured Protobuf / validated equivalent messages internally; wire parsing should be strictly typed, size bounded and not split on unescaped user-supplied material strings. Policy updates from 4B remain remotely possible but cannot transfer material.

The orbital UI displays each vehicle's destination, pilot, passengers, load/seat utilization, fuel/condition, ETA in ticks, in-flight status and durable latest receipt; no planet scene is generated.

## 6. Mandatory tests

1. Build vehicle using *existing* finite fabricated resources, verify ship depletion and conserved dry mass.
2. Load two finite named NPCs and supplies; verify NPC physical custody never duplicates.
3. Attempt overweight manifest, too many seats, unavailable pilot, wrong body frame, stale region revision and unauthorized owner; all rejected with no side effect.
4. Launch/descent/arrival/reload/ascent with exact material accounting and fuel/residue balance.
5. Keep farm and extractor running only with resident, eligible workers; stop when the relevant workers are in transit.
6. Restart during each phase (before load commit, after load, mid-descent, landed before unload, ascent, after arrival): precisely one manifest and completion.
7. Reissue same request_id and repeat arrival ticks: no double delivery, no repeated fuel debit or repeated custody moves.
8. Simulate fuel shortage, damaged vehicle, missing/closed pad, exhausted reserves and absent local life support; no implicit cargo transfer.
9. World-level conservation includes ship stocks, site stocks, shuttle dry mass, manifests and spent fuel residue; no artificial losses.
10. Native Windows and Linux build/CTest, HTTP relay validation, browser telemetry, end-to-end authenticated orbit-to-ground trip at the exact PR head.

## 7. Delivery slices

- **4C.1:** native shuttle domain types and exact invariant tests; no integration claim.
- **4C.2:** finite ship fabrication, load/unload and NPC custody operations through Simulation.
- **4C.3:** deterministic flight scheduler and persistence bump to v80 with migration from v79.
- **4C.4:** authorized host commands, relay and orbital UI.
- **4C.5:** full crash-injection, restart and Windows/Linux gameplay acceptance.

Do not mark 4C complete on the basis of unit tests alone. Keep PR #48 draft until exact-head native acceptance is green. Do not change 4B's remote workforce policy into remote cargo transportation.

## PLAY-13D.4C.3 native integration checkpoint

Implemented on draft PR #48, pending exact-head Windows/Linux acceptance. This is a **first bounded shuttle flight slice**, not the full general multi-passenger, multi-cargo vehicle system described above.

- The existing v80 player-fabricated shuttle is extended to a v81 canonical flight record, with four phases: orbital (0), descending (1), landed (2), ascending (3).
- An authorized launch uses the real committed `PersistentWorld` copy/validate/SQLite-commit/publish path. Successful launch schedules a deterministic three-tick journey in `Simulation::step`; arrival is host-driven, not browser-driven.
- The pilot is one **existing named `NpcRecord`** (the same finite NPC identity/custody framework used for migration and ferries). A dedicated shuttle transit custody location represents the pilot during flight, and landing changes that exact record to the actual region's surface custody location. Neither launch nor landing creates a second NPC. This first slice does **not** reuse ferry `VoyageRecord` directly because it models ship-to-surface shuttle states rather than ship-to-ship passenger ferries; a common transport receipt abstraction remains a future consolidation opportunity.
- The 1,000 kg shuttle's water/food manifest stays inside the vehicle during flight. Loading and unloading use ship inventory while orbital and site inventory while landed. No automatic or remote transfer happens at arrival.
- Simplified trip propulsion uses 1 kg of **already-loaded water per launch** and converts it to wastewater at the *departure* endpoint (ship or site); all existing matter is conserved. This is deliberately a provisional water-propellant gameplay recipe, not a real thruster/fuel engineering model.
- While landed, the pilot consumes one unit each of local food, water and oxygen every 20 committed ticks; resulting organic waste, wastewater and CO₂ remain on site. The existing ship physical-headcount consumption excludes the absent pilot.
- Flight request IDs are persisted in a bounded, nonreusable registry, plus per-shuttle descent/ascent receipts. Replays are rejected without mutation. Future wire-level request lookup can return the original receipt without a second commit.
- A parent ship cannot begin a celestial transit while its shuttle is descending, landed or ascending. Future independently supplied orbital landing platforms may relax this constraint.
- Snapshot parser validates v81 phase, due tick, pilot identity, receipt linkage and region coordinates. Legacy v80 worlds remain readable.
- Added tests for authorization, failure-injected takeoff rollback, custody while in flight, unsupported midair load/unload, phase progression, in-memory snapshot reconstruction, cargo delivered to the actual site, return flight, request replay rejection and committed SQLite restart of final state.

**Still outstanding:** full crew/passenger manifest and capacity, boarding of additional NPCs, worker recall and task reassignment, richer local fuel and flight failure outcomes, robust crash injection at every SQLite stage with a separate process restart *during* the flight, host/relay/browser commands (4C.4), and exact-head Windows/Linux CI verification. Do not label this first slice as production-complete until those are closed.

## PLAY-13D.4C.4 — Live orbital controls checkpoint (2026-11-04)

The live C++ colony host now accepts authenticated `PLANET_SHUTTLE|body|lat|lon|operation[|...]` commands. Supported operations: `STATUS`, `BUILD`, `PILOT|npc_id`, `LOAD|WATER/FOOD|kg`, `UNLOAD|WATER/FOOD|kg`, `DESCEND|request_id`, `ASCEND|request_id`. Exact field counts and input bounds are checked both in the HTTP relay and C++ native host. All mutating operations invoke the existing `PersistentWorld` SQLite-backed APIs, not an independent UI or relay world. Status reads require owner/site association and expose only committed state. Receipts include committed world generation.

The relay advertises `colonyPlanetShuttle` in the session command contract, rejects unknown fields and wrong JSON types, disallows unsupported materials, and forwards each valid request to the private authenticated host pipe. HTTP acceptance (202) means **the input was queued**, not that the world operation succeeded; the native `PLANET_SHUTTLE_COMMITTED` or `ERROR|PLANET_SHUTTLE_DENIED` telemetry is authoritative.

Planetary orbital cards now provide a shuttle control panel for construction, pilot selection, finite material loading/unloading, launch and return. Native `PLANET_SHUTTLE_STATUS` includes presence, phase, pilot, manifest, due tick, active flight ID, durable descent/ascent receipts, and world generation. The host additionally broadcasts transitions after actual committed `Simulation::step` arrivals; no client-side timer fabricates landings.

The existing planetary relay and Node test suites have been extended with shuttle request validation, forged-token refusal, malformed frame rejection, and receipt contract assertions. Native Windows/Linux acceptance at the **exact head SHA** remains mandatory before claiming this gate verified. Full gameplay acceptance with a physically built site and actual UI click-through remains a separate E2E requirement. Shuttle flight currently supports one pilot and water/food cargo only; no second passenger, direct space landing, fuel tank or graphics renderer is claimed.

## PLAY-13D.4C.5 — Real host/relay acceptance harness

New native fixture `galaxy_play13d_shuttle_host_fixture` seeds an **actual SQLite world** with a persistent physically catalogued celestial body, a financed two-NPC surface spaceport, finite life-support supplies and two manufactured modules ready for shuttle fabrication. It produces real body/region, ship, pilot and initial mass values. Its independent `--verify` mode reopens the same SQLite database after both host sessions and checks world material conservation, the returned named NPC's custody, shuttle manifest and immutable launch receipts.

`tests/PLAY13D_ShuttleLiveE2E.py` starts the actual `galaxy_colony_live_host` behind the real authenticated `interactive_relay.py`, subscribes to **native SSE events**, and exercises build → pilot assignment → load water/food → descend → confirm touchdown → unload into the surface inventory → reload surface water → ascend → return to orbit → unload into the ship. Each queued HTTP 202 requires a separate `PLANET_SHUTTLE_COMMITTED` and `PLANET_SHUTTLE_STATUS` from the native writer. The acceptance includes forged-token, invalid manifest, invalid request, idempotent replay denial, genuine second-process restart and an independent native SQLite/material/pilot audit.

A previously hidden blocker was fixed: the relay's advertised planetary actions were absent from its **colony-mode POST allowlist**, so even well-formed UI/relay shuttle commands would have been rejected with 403. `live/interactive_relay.py` now includes the authenticated planetary and shuttle operations in that allowlist. This remains owner-only; it is not a multiplayer authorization bypass.

`.github/workflows/play13d.yml` now compiles the native fixture on **Windows and Linux** and runs the real E2E script on both operating systems, in addition to existing CTest, Node frame parser and HTTP validation.

**Acceptance condition:** exact-head native Windows and Linux CI must actually finish successfully, the E2E must show native commitment rather than mere relay queuing, and the independent restart/mass audit must pass. No completion claim is made while the jobs are still queued. Remaining functional limits of v81 (single named pilot, provisional water propellant, water/food cargo, no general passenger manifest or crash injection at each native SQLite boundary) are unchanged.

## PLAY-13D.4C.6 — Desktop-native shuttle command boundary

The browser remains a **temporary development and test harness**, not the planned shipped PC game UI.

The new header-only `include/galaxy/OrbitalShuttleCommands.hpp` defines a typed, in-process `OrbitalShuttleCommand`/`OrbitalShuttleReply` service for the future standalone desktop client. The service accepts a trusted host-authenticated principal, ship, exact `PlanetarySiteKey`, operation, and bounded arguments. It checks the existing canonical site ownership and delegates every mutation to `PersistentWorld`; it does not own another simulation, SQLite database, cargo ledger, or NPC roster. Status reads report real committed phase, NPC identity and cargo via `PlanetaryShuttleState`.

The temporary `colony_live_host.cpp` wire parser now adapts validated `PLANET_SHUTTLE` commands into that **same native service**, and existing Web/HTTP telemetry only serializes its result. Direct `PLAY13D_RegionalFacilitiesTests.cpp` tests exercise the typed service without HTTP, including owner/region denial, invalid cargo, real committed cargo loading and descent. A custody defect was additionally closed: the pilot cannot be reassigned unless the shuttle is docked in orbit.

This is a game-core integration boundary for a future desktop renderer/UI, **not a completed standalone desktop window**. No browser rendering dependencies were added to the game kernel. Keep the exact-head Windows/Linux native CI acceptance open pending real results.

## PLAY-13D.4C.7 — Reuse existing NPC transport manifest and custody

Shuttle riders now use the same **authoritative named NPC records**, ordered `passenger_ids`, custody projection, finite occupancy, replay-guarded flight IDs and transactional persistence principles already employed by the NPC ferry/migration system. This is not a second NPC population, and it does not invent workers on arrival.

- New `PlanetaryShuttleState::passenger_ids`: sorted, unique, at most three named NPC passengers **plus** the previously assigned pilot. The game authenticates the orbital owner/site/ship and checks crew role, physical home berth, healthy duty eligibility, lack of competing ferry transport, duplicate shuttle booking and dependent guardianship.
- `Simulation::book_planetary_shuttle_passenger(...,board)` and its `PersistentWorld` wrapper handle booking/release as existing SQLite-backed clone/commit/publish transactions. They reject changes once the shuttle departs the orbital berth. Existing NPC duty scheduling treats booked people as unavailable, preventing a second physical assignment.
- Both descent and ascent move each **same NpcRecord** to the shuttle's existing transit custody address, then to the real surface-site location or parent-ship berth at the committed arrival tick. A shared `npc_custody` projection publishes the current flight request as the passenger's active transport ID **without falsely setting the ferry-specific `NpcRecord.transport_id`**.
- The canonical snapshot advances to `galaxy-kernel-v82`, preserving old v81 saves while persisting every ordered passenger ID. Restore validation and `total_ore` independently check duplicate passenger identities, site/ship match, location, assigned role and opposing ferry claims.
- Surface food, water, oxygen, and real waste consumption now scale to the physically present pilot **and** passengers, with shortages stalling support rather than spawning free supplies.
- `OrbitalShuttleAction::{BookPassenger,ReleasePassenger}` exposes booking through the native desktop command service; the temporary HTTP relay supports strictly validated `BOARD|npc`/`RELEASE|npc` commands and the test UI renders the reported sorted roster.
- Updated native regional facilities tests cover booking and failed SQLite publication rollback, unauthorized/duplicate booking, in-flight modifications, manifest persistence, actual landing/return custody and releasing the passenger at the orbital berth. The real native host/relay E2E fixture now books a passenger and independently reopens SQLite to verify the same named person and v82 manifest after restart.

**Important limitation:** this first manifest implementation transports boarded passengers on the shuttle's full round trip. It does *not yet* change permanent NPC home-colony/work assignment or detach an individual passenger to become a new surface worker. Disembarkation/colony role transfer should explicitly reuse the established NPC migration/acceptance transaction so surface settlers are not silently duplicated or stranded. Similarly, ferry-specific `transport_id` is intentionally not set for a planetary leg: existing ferry validations require a separate real ship and colony-to-colony endpoints, which a planetary shuttle does not have.

Windows and Linux CI on the exact latest commit are still required before claiming successful native acceptance; tests added to the branch are not evidence of a passing run.

## PLAY-13D.4C.8 — Persistent surface visitors, explicit temporary duty and physical return pickup

A real planetary shuttle now performs **two separate NPC custody operations** while landed at an operational spaceport: `DISEMBARK` removes a named NPC from the embarked passenger manifest and commits that same person to the site's `disembarked_ids` ledger, and `REBOARD` requires an available seat and the physically present visitor before putting the identical NPC back on the manifest. The disembarked NPC stays at the **surface location** even after their shuttle ascends, and the ship must actually descend again to collect them. Boarding clears the person's temporary surface duty automatically. The pilot cannot disembark through this passenger operation.

Authoritative `Simulation` and `PersistentWorld` methods use the existing copy/validate/SQLite-commit/publish transaction and enforce owner, ship, site, unique named identities, operative spaceport, landed phase, and guardianship exclusion for dependents. No new `NpcRecord` is created, no cargo teleport occurs and no directly controlled player avatar is sent to the surface. Failed SQLite publication must preserve pre-command world state.

The distinct `DUTY_ON` and `DUTY_OFF` operations explicitly delegate or revoke **temporary help** by a disembarked visitor. Surface presence does not imply employment, permanent reassignment or immigration. The standing site still has its existing two permanent delegated worker slots; fit temporary helpers count toward the physical crew available for site work without fabricating new employees. All physically present visitors draw finite site food, water and oxygen each support cadence and generate corresponding physical waste even if the shuttle is in orbit. Depletion pauses work instead of creating supplies. The native command service owns the typed operations; the temporary HTTP/browser interface merely forwards commands and displays native manifest, visitor and duty telemetry.

`galaxy-kernel-v83` introduced the ordered `disembarked_ids` ledger; `galaxy-kernel-v84` adds sorted `duty_ids` as a strict subset. Both are authenticated against the same authoritative NPC custody and are backwards-readable alongside prior shuttle snapshots. Restart validation rejects duplicate identities, missing NPCs, mismatched physical locations, unrelated colony assignments, forged active ferry claims, and workers not actually registered on the surface.

Acceptance tests in `tests/PLAY13D_RegionalFacilitiesTests.cpp` exercise disembark denial, committed visitor custody, roundtrip save/restore, failed SQLite publication rollback, separate work assignment/rollback, ascent leaving a visitor behind, second physical descent for pickup, removal of duty on reboarding, and return-to-orbit of the exact same named person. `tests/PLAY13D_ShuttleLiveE2E.py` covers real native host/relay `DISEMBARK`, `DUTY_ON`, `DUTY_OFF` and `REBOARD`. Browser and HTTP schema tests reject malformed new commands and frames. The independent native restart fixture now demands v84.

**Scope boundary:** A disembarked visitor on temporary duty remains a member of the parent ship's assigned workforce, not an admitted permanent planetary settler. Permanent change of residency, migration consent, dependent travel, guardian co-manifests and expansion of the two standing surface worker slots are distinct future operations. Current transport intentionally rejects guardian embarkation where dependent travel approval would otherwise be required; it must never silently transport or strand children. The implemented gate is not certified complete until exact-head native Windows/Linux CI successfully runs; queued workflows do not count as execution.
