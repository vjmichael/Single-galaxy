# Group 3 — Mining → Industry (execution register)

**Branch:** `sg/group3-mining-industry-20261111`, founded from independently hosted Windows/Linux VIS-01 source `b5d7057928d87d3cb1a05e07ff1d784a9425ad5f`. Do not merge the parked experimental PR #124 wholesale: its 04A source predates v125 swept physical collisions and SGDX native receipts. Reconcile semantics and preserve the accepted flight/world code.

**Non-negotiable:** Only the original Simulation/PersistentWorld can own rock ore, fitted modules, ship cargo, station hangar lots, refinery output and physical NPC/workforce capacity. Godot renders and requests commands only. No free drill, ore, replacement hull or silent save upgrade. All modules/refining require finite material and operative power/crew. All outcomes require exact original journal receipts, SQLite recovery, replay/tamper refusal and mass balance. Owner's physical pilot baseline is sufficient to advance engineering; do not repeat basic boarding tests as a development gate.

## Group 3A — Actual ship fitting and module manufacturing (active)

- Original v126 **genesis-only** mode allows one manufactured type-4 mining drill in a real hardpoint (2 physical tonnes per module). Existing v125 and older saves are **not** automatically upgraded.
- Actor must be an owned original colony ship, docked at an existing station with stable jump state and at least two crew. An actual `FabricateModule` command converts previously refined physical tonnes to an inventory module; `ConfigureMiningTool` atomically installs/removes that inventory module. Hardpoint power uses the existing trusted ship-status engine, not a UI toggle.
- Acceptance: genuine native Windows/Linux compile, original C++ material conservation, no-drill/no-mass denial, no duplicate module, unsupported slot, no ownership/crew, stale command/replay, SQLite reopen and signed world integrity.
- **Known game-content dependency:** the existing station test genesis contains no gifted drill, refinery, or mine-ready refined stock. This is intentional. Manufacturing a first drill requires real external physical supply or a separately authorized material-backed starter production/contract chain. Do not claim a new world is immediately mineable without that bootstrap.

## Group 3B — Asteroid survey

- Current `SpaceMiningContact` is a **30-metre proximity/collision readback**, not long-range sensor. Add an original C++ read-only bounded survey with same-cell 3D coordinates, mass/rock geology and ownership/provenance; return a target ID and bearing/range through the supervised pipe.
- Godot 8.5km render capability is **not** resource discovery. Display only original surveyed asteroids with sensible LOD; never create terrain minerals or mining targets locally.
- Acceptance: source-backed distant target, stale-session denial, exhausted/depleted omission, exact tick/journal join, no scan without real hardware/authorization.

## Group 3C — Ship-only finite extraction

- One authenticated seated native pilot + an operative powered drill may request `MINE` against the last verified asteroid ID. Original kernel validates same physical frame, effective hull-to-surface range, cargo capacity, remaining ore and exact `amount=1 tonne`.
- A successful original SQLite event subtracts exactly one tonne and adds exactly one tonne to that ship plus **one nonduplicating geological lot**. No hand mining, automatic yield, client-authored mineral stock or multi-tonne command. Reject power off, lost tool, docked ship, lost hull, unauthorized actor, replay and depletion.
- Acceptance: real original C++ ship/rock/lot mass balance, sensor/command pairing, durable shutdown and restart, matching Godot receipt.

## Group 3D — Physical cargo custody

- Reuse the **existing** original dock/undock, `UnloadOre` and station custody ledger: source ore is held on the ship until docking and only enters an existing station hangar on an authorized physically present owner action. Existing `REFHEAD/REFLOT/REFMIN/REFEND` protocol remains source of truth.
- Acceptance: exactly one tonne through original ship, station and SQLite, wrong station/ship/empty hold/foreign owner/duplicate unload refused, no instant teleport.

## Group 3E — Station refining and manufacturing

- Reuse `StationRefineryBatch`, original `RefineStationOre` and existing station terminal. Processing requires an actual installed/staffed/powered station facility, one source-tagged physical lot, energy consumption and output named mineral stock exactly 1,000 kg with existing recipe fractions and tailings.
- Connect finished minerals to original industry manufacturing and the 2-tonne first-drill supply chain through real inventory/work orders. Never silently reload the station's tonne aboard the ship to pretend refining was station-owned.
- Acceptance: original mined tonne -> dock -> unload -> station staff/power -> refine -> durable named output -> manufacture eligible goods, verify both Windows/Linux, interruption replay, mass conservation, source provenance and saved-world reopen.

**Status:** Group3A kernel integration underway. Group3B–E have dependent original code and test evidence but are not yet a complete player-controlled production vertical slice. No release, main-merge or physical R9700 claim.


## Binding manufacturing-design reconciliation (2026-11-11)

**Do NOT promote the first engineering module to a final manufactured drill.** User-required design sources are `docs/design/PLAY11_SHIP_FITTING_EXPLORATION_AND_TRAVEL_ARCHITECTURE_2026-11-04.md`, `docs/engineering/INDUSTRY_SUPPLY_CHAIN_RECIPE_RESEARCH_20261106.md`, `docs/engineering/INDUSTRY01_WO1_SOURCE_CLOSEOUT_20261106.md`, `docs/engineering/ECON02B1_Factory_Design_And_Next_Gates_2026-10-31.md` and `docs/engineering/PROJECT_DEVELOPMENT_SEQUENCE_20261106.md`. Together these specify:

- One original material/recipe registry, **100 item families, 71 standard candidate recipes, 3 lossy recovery recipes, 8 services, 28 explicit legacy identities**. Only two current `industry01::original_workshop_recipe` bindings are authorized by the original production engine: `native_cut_aluminum_panel` and `native_shape_aluminum_frame`. All other catalogue recipes remain candidate data, not allowed machine commands. Original workshop T1–T5 sizes and batch minima, actual NPC skills/time, site custody, machine, utility, maintenance and output/scrap room govern actual batches. T1–T5 are **real operational machine capacity choices**, not arbitrary technology unlocks or predefined building sizes.
- The **PLAY-11 introductory module-equivalent** is a physical 2-tonne construction module reserved/fitted in a real hardpoint, an explicit **temporary material-accounting stand-in**, not an approved mining-laser/drill bill of materials. The v126 `ConfigureMiningTool` experiment demonstrates owner/cargo/power/restart consistency but does not establish a researched or approved `assemble_mining_drill` recipe. The 2-tonne test-only starting refined stock exists solely in explicit disposable fixture genesis; production worlds never receive it. Keep a final player-ready drill *manufacturing* completion gate open until the user-approved catalogue defines exact inputs, output, requisite machine/skills and site handling and native code executes that recipe.
- **PLAY-11 original powered survey scanner is 50,000 mm**. The 8.5 km Godot render distance is **not** scanner capability. A ship without a powered hardpoint-1 exploration scanner receives no long-range `SURVEY` observation; original 30m passive asteroid contact remains a separate mechanism. Current 3B tests explicitly deny hardware-free survey and test legitimate mass-backed scanner installation.
- The original station refinery's source-tagged **one tonne** yields **1,000kg of specific original concentrate names**: e.g. metallic `nickel_iron`, `troilite`, `olivine`, `pyroxene`, `other`. These are **not** identical to `bulk/aluminum` or other INDUSTRY-01 candidate feed. Do not construct any hidden `nickel_iron → aluminum` or `1 tonne → construction module` recipe; no manual player-facing inventory edit. Station `mineral_kg` is genuinely owned local stock; physically authorized original-world **mineral transfer and approved conversion/plant processing** must be designed and implemented before that source can enter an INDUSTRY-01 workshop.
- Existing `RefineMineral` and `TransferWorkshopFeed` are authoritative original actions for onboard mineral refinement / physically constructed workshops, and `native_cut_aluminum_panel` produces 19kg actual panel and 1kg physically stored scrap from 20kg original aluminum when staffed/powered. This *already approved* production capability may be validated, but it **cannot** count as using the new station source lot until material transport/identity and mass are durably linked.
- New `tests/Group3StationCustodyE2E.cpp` carries the finite **engineering** module via actual C++ transactions and follows mine→ship→dock→unload→staff/power/refine→1,000kg station concentrate with SQLite tamper/restart tests. Its explicit catalogue negative assertions deliberately prevent a false claim that a finished drill or aluminum item was created. It is **not** final full-chain manufacturing acceptance.

**Revised gate:** 3A–3C protected native executable and Godot parser tests on exact head Windows/Linux; 3D/3E original-source replay, wrong-owner denial, crash/rollback/restart and conservation; then a separately specified, source-grounded station-material transfer + named native production batch with actual NPC/machine/power/waste and same-source provenance. A ready physical Windows package requires those plus human R9700 acceptance. PR #133 remains draft, source-only until actual runner receipts pass, no merge to `main`.

## Owner-approved reprioritization — resource collection and refining first (Nov 11)

**Modules/ship fitting and manufacturing are explicitly deferred.** No mining-drill BOM, fixed module 2-tonne recipe, ore->aluminum conversion, new factory, or free starting resource may be introduced as a substitute for design. The v126 experimental fitting test harness is retained as research evidence only, NOT enabled in the normal installed game's new-world seed. The normal source-lot collection path uses the already-implemented original v123+ ledger / current physical 3D ship authority.

### Immediate resource loop

1. **Physical original rock discovery**: existing passive 30m proximity contact for direct collection and existing 50m PLAY-11 powered scanner for detailed survey. 8.5km camera rendering is strictly visual.
2. **Finite ore**: original `Mine` consumes exactly one geological tonne from the original asteroid and places exactly one tonne in actual ship cargo; owner, range, same frame, replay, capacity, and physical collision apply in the authoritative kernel. The multiplayer/automated-mining and v126 tool prototype are deferred.
3. **Actual custody transfer**: return on the real ship and commit `RequestDock`, `UnloadOre` to the owner-specific station hangar with the same immutable rock/ship/owner/mining and unload receipts. No source lot is a second mass inventory.
4. **Real refinery**: only a previously built facility with staffed operators, live utility power, and a complete source-tagged one-tonne lot may commit `RefineStationOre`. The source tonne produces exactly **1,000 kg** named geological minerals, owned by that station, with the existing source profile. No automatic return to ship or manufactured product.
5. **Industry material-identity audit (read-only)**: `StationMineralIdentityAudit.hpp` checks each original mineral's exact `mineral/<name>` legacy identity, kg unit and 1,000 g-per-kg contract against the pre-existing INDUSTRY01 legacy register, validates plant owner and process totals, and always reports **NOT_APPROVED_MANUFACTURING_FEED**. It creates no new inventory or conversion recipe. `Group3StationMaterialIdentityTests.cpp` passed actual Linux C++ compilation and execution against the unchanged industry catalogue.
6. **Full resource E2E**: `Group3StationCustodyE2E.cpp` is now derived from the previously accepted original PLAY-STATION-03B native C++/SQLite test plus station-material source checks. It passed actual Linux compilation/execution against the accepted native kernel: true mining, ship->hangar original custody, built/crew/powered refinery, exact 1000kg concentrates, crash rollback, independent database copy/restart and forged ledger rejection. New exact branch Windows/Linux CI remains queued and is NOT passing evidence.

### Future gated work (do not substitute shortcuts)

- Real human factory/workshop material feed from station concentrate needs its **own explicit physically valid conversion and native approved recipe**, with worker/machine/energy/byproduct and exact source-lot input. Candidate INDUSTRY01 recipes are not automatically authorized.
- Fitting, advanced modules, drill model, manufacturing, full ship-asset visuals and downstream goods come **after** collection/refinery correctness. Do not block this resource acceptance on finishing undisclosed module designs.
- Finish one native Windows/Linux exact-head batch and Godot host/parser acceptance; retain PR draft and no release authorization until genuine results are reconciled.

### Additional physical native pilot evidence (resource-first)

The normal original native game uses v123+ geological ore/source custody, without enabling experimental v126 mining-drill fitting or seeding free modules/refined material. New `Group3ResourceMiningSeed.cpp` creates an explicitly finite geological rock and a real, owned, crewed, docked colony ship with **zero modules and zero refined stock**. New `Group3ResourceMiningPipeTests.py` drives the real child C++ pilot across seat/undock/flight/one-tonne MINE (no module installed), independently verifies ROCK and SHIP stock, stale request refusal and SQLite restart. This is a native executable CTest, NOT a fake frame or static source check; acceptance awaits exact SHA MSVC/Linux execution.

A demonstrated true Linux defect in the original station's new-world replay was independently reproduced: `snapshot: ship NPC count mismatch` because individual workforce initialization occurred before a second finite crew was added. Reordered `enable_individual_workforce()` after BOTH original ship rosters in `src/desktop_station_host.cpp`. Native Linux station boarding, original refiner pipe and physical unload pipe now **3/3 PASS** using the corrected installed-source-shaped native host and exact original accepted kernel. Do not treat this as GitHub exact-head Windows acceptance.
