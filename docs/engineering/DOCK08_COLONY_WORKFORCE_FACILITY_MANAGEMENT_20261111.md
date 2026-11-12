# DOCK-08 — Colony Industrial Workforce and Facility Management

**Engineering register, 2026-11-11. Development-only source staged; acceptance OPEN.**

This group follows DOCK-07. DOCK-07's source-only inventory/production interface is provisionally parked, not treated as native-compiled, installed, or released. All changes remain on the single DOCK development branch; do not merge to main or launch repeated routine CI/PR checks until the consolidated engineering acceptance batch.

## Design and original authoritative boundaries

One **I** colony interface controls the exact original station represented by the authenticated player's physically present `PlayerPerson.physical_station`. This is *not* a new player-wide, galaxy-wide or remotely selectable inventory. `Simulation`/`PersistentWorld`, the existing SQLite save and the original immutable command/effect/resource ledgers remain the only authoritative physical facilities, source materials, named NPCs, construction outputs, work shifts and events.

`include/galaxy/ColonyIndustrialManagement.hpp` provides a bounded 28-operation read-only preview and native proposal builder. The source ship is derived only from `built_outposts[station].source_colony_ship`, rechecked against a real owned `ColonyShip`, and required to be physically docked at this original station for every management command. The UI cannot choose arbitrary NPC IDs, sites, quantities, construction materials or production recipes. The native final `PersistentWorld::submit` and `step` always have final authority to refuse a proposed action.

## Integrated original facilities and material requirements

Player-originated one-use, committed commands cover actual **ConstructWorkshop**, **ConstructStorage**, **ConstructUtility**, **ConstructHabitat**, **ConstructWorkshopFinisher**, and **BuildStationRefinery**. Required modules are checked conservatively before nomination, then *the original kernel* verifies and consumes genuine source-stock/embodied materials; a displayed building or owner approval alone cannot mint one. The founding ship still must be physically present at the original site.

Real habitat provisioning uses `TransferHabitatStock` food, water or oxygen in bounded 100-kg installments (allowed regardless of whether existing beds are full); `AssignHabitatResidents` transfers one real eligible finite resident at a time. `AssignUtilityTechnicians` delegates one already housed resident, `TransferUtilitySpares` provides one original existing module, `SetUtilityMaintenance` authorizes native ongoing maintenance, and `RepairUtility` requests original material-backed repair. Every effect remains constrained by original staff, life-support, power, condition and site-local capacity rules; no client-created utility power is granted.

**Facility selection is restricted to the current ORIGINAL CONSTRUCTED station.** Existing generic starter stations are not silently converted into manufactured outposts; constructing an actual outpost and reaching that original physical colony is still necessary when an existing starter location has no outpost authority.

## Named industrial workforce, qualification and shifts

Only the two existing original native workshop trades are exposed: panel cutting and frame shaping. Every human-selected operation reuses original `workforce02` and Simulation records, without creating employees.

- `TrainWorkshopTrade`: selects a real free eligible site resident and consumes actual machine input/utility, earning original immutable per-NPC trade credits. Three source-backed credits are required before native supervisor certification.
- `AppointWorkshopSupervisor`: only a real, local, certified and available original named NPC may be selected, with a cap of 4096 inspected original records. A truncated scan refuses rather than fabricates a worker.
- `StartWorkshopShift`: offers a 16-tick original worker commitment (or an eligible expired-slot renewal), checked for existing medical/yard/workshop duty and overlapping shifts by original helpers. Scheduling never transfers a person from another colony.
- `AssignWorkshopOperator` / `AssignWorkshopFinisher`: owner-approved original available worker nomination; distinct release actions request original worker removal and cannot bypass original source occupancy checks.
- `CancelWorkshopJob` cancels one genuine prior owner-approved job. `CancelWorkshopShift` acts only on an original identifiable shift. The UI never regards a preview as a committed shift or an approved program as delivered manufactured goods.

DOCK-07's one-batch approval, original production ticks and source-backed physical workshop/warehouse transfers remain integrated in the SAME interface.

## Native C++ pipe and Godot interface

`src/desktop_station_host.cpp` adds `IMVIEW` and `IMAPPLY` under the existing exclusive SQLite writer lease and original `PERSON` session, monotonic request, expected generation, locality, rate and command custody checks. A selected native action is submitted once, stepped once, and is accepted only on its matching original `Event.command_id`. Rejections remain rejections. All management snapshots come from immutable-after-read original committed memory; no shadow work ledger was introduced.

`IMHEAD`, `IMWORK`, `IMOPT` and `IMEND` carry the original current station, physical source ship/berth, built facilities, stored energy, finite materials, named staff, qualification, shift horizon, per-operation conservative eligibility, exact request, tick and generation. `desktop_godot/world/native_station_session.gd` validates field counts/identity and paired receipt before emitting a UI snapshot. An automatically queued management read follows the original inventory read when the existing **I** panel opens; one station writer prevents concurrent side effects.

`refinery_terminal_ui.gd` exposes the original installed facility / worker readiness and explicitly selectable one-use commands; unavailable items are disabled. The UI describes named worker and workshop gates in plain language. No client-side employee, output, credential, stock or plant is ever generated. `main.gd` binds UI actions to the existing authenticated station session.

## Staged acceptance (NOT EXECUTED)

`tests/Dock08ColonyManagementTests.cpp` and CMake `galaxy_dock08_colony_facility_workforce` stage: physically mined/refined modules, native material-consuming outpost, workshop, storage, utilities and habitat construction, finite original ship-to-habitat stock, utility staff, actual industrial aluminum workshop feed, original docking and registration, owner/remote/duplicate/invalid-selector denials, non-mutating status reads, real maintenance authorization, three original per-NPC training receipts, certified supervisor appointment, a finite shift proposal, material conservation, immutable source identity, SQLite restart and equal source snapshots.

**Not run and therefore not claimed:** compiler diagnostics, actual CTest execution, Windows/Linux native tests, actual end-to-end named-worker production under an installed UI, fault-injected restart, physical Radeon AI Pro R9700 acceptance, final designer-approved colony, or release validation. Any demonstrated failures during the later consolidated batch require repair before a passing verdict.

## Remaining functional dependencies

1. The initial starter station may not be an original constructed outpost; the player must build and physically reach an actual constructed site before these site-bound original services are eligible. A reviewed local outpost-foundation workflow and transport/registration UX remain open.
2. Native workshop staffing and shift helpers are reused, but no automatic recruitment/qualification occurs. Station food, rest, player-owned residence, competing dock and medical duties, supervisor relief, and future pre-scheduled shift cancellation still need cross-service lifecycle verification and interface extensions.
3. An active one-batch original manufacturing program cannot prove its final physical output until real machine, power, input, supervisor, worker, output capacity, schedule and journal evidence have all been run and examined.
4. Native station refinery concentrates are NOT automatically feed for aluminum workshops. A real approved transformation must be researched, built, and proven separately.
5. Original workshop/storage logistics beyond the five DOCK-07 main-bin movements remain separate physical logistics and cross-colony transport tasks. No galaxy-wide inventory has been created.

**DOCK-08 status: source implementation provisionally complete, acceptance open.** The next engineering priority is a genuinely player-accessible constructed-colony onboarding and workforce/production vertical slice, then later consolidated exact-head native and installed acceptance.