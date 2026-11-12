# DOCK-09 — Constructed Colony Onboarding & Industrial Production Vertical Slice

**2026-11-11 | Source implementation staged, consolidated acceptance OPEN.** No native compile, CTest, Windows installed play or physical Radeon owner acceptance claimed. DOCK-07 and DOCK-08 remain parked provisionally. No release/merge to main authorized.

## Actual playable source path

DOCK-09 connects the existing starter-station -> real cockpit -> real outpost -> same colony **I** panel -> real worker/production output. It does **not** convert the generic starter station to a constructed colony, spawn a factory or secondary cargo ledger, or bypass the physical flight/dock commands.

1. From the original starting station, board the EXISTING colony ship and take the verified cockpit seat. Undock via existing original native flight control.
2. Navigate to source geological asteroids using original ROCK/NAV telemetry and original flight inputs. Mine real tonnes within physical reach. The original station host's **new-world-only genesis** now seeds two nearby physical, finite original metallic and silicate deposits of radius **2 metres**, respectively densities **3,000 kg/m³** and **1,000 kg/m³**. Mass is calculated by the original `asteroid_mass_tonnes` function. Existing saves are **never** reseeded or overwritten. No drill, ore, refinery or free module is gifted.
3. The cockpit's original material panel reports actual hull cargo, refined tonnes, discrete original modules, plagioclase, aluminum and original built outpost identity. Four fixed, user-selected, one-use commands are available only after original seated pilot/seat revision, session, tick and source identity checks:
   - **Refine up to 4 mined cargo tonnes** via `Action::RefineOre`.
   - **Fabricate up to 2 original modules** via `Action::FabricateModule`, consuming 2 refined tonnes each.
   - **Found one original outpost**, consuming **4 original modules (8 ore-tonne equivalents)** via `Action::ConstructOutpost` from a stationary real ship in space. Original engine verifies the immutable current position and that the prospective station is not too close to existing stations. Move clear of any rock before selecting the build site.
   - **Separate original plagioclase** via `Action::RefineMineral` code 10, consuming up to 12,000 kg of real geological source per selected command. Existing original fixed native recipe yields **20% aluminum, 60% glass, 20% native tailings**. No station mineral concentrate is allowed as substitute.
4. Once the original outpost exists (its ID is read from `built_outposts`), use existing physically verified flight and **RequestDock** near that actual new station. Leave the original ship at its actual dock, then open **I**. The existing host reopens the same SQLite world; the only acceptable authoritatively selected site is the original `PlayerPerson.physical_station`.
5. Select native, material-consuming DOCK-08 operations to install **workshop, warehouse, utility, habitat**. Fuel and construction are finite: the first physical outpost costs 4 modules; workshop and warehouse need additional original modules; utility and habitation consume additional original resources. The new-world-only starter ship also carries finite, original genesis food/water/oxygen stores for genuine habitat provisioning; not a worker spawn. Supply the habitat and assign named original residents, including utility technicians.
6. From the docked founding ship, move 20 kg original aluminum into the original warehouse then workshop using DOCK-07 physical transfers. Train eligible named residents in native panel cutting (three original `TrainWorkshopTrade` receipts consume original workshop materials and utility), appoint a certified supervisor and schedule a genuine 16-tick operator shift. These operations cannot fabricate an NPC, certificate, supervisor, or power.
7. Replenish the workshop's consumed material, approve one original **20 kg aluminum -> 19 kg panels + 1 kg scrap** batch, then click **Run 16 original workshop ticks**. The writer commits 16 real `PersistentWorld::step` operations, checks exact original material mass preservation, and reads actual `completed_batches` and physical stock. The result codes distinguish **physical batch produced** from **16 ticks elapsed without completed output**. The player can move actual produced panels to the same colony warehouse; no UI-updated stock is accepted.

The I panel's DOCK-09 status guidance reports the next source-backed step based on the original local inventory and management view; it does not independently award tasks or execution authority.

## Engine changes

- `include/galaxy/ColonyOnboarding.hpp`: immutable material/outpost readback and preflight proposal builder for 4 tightly selected original ship actions; no new simulation inventory tables or stateful controller.
- `src/desktop_player_host.cpp`: fixed `ONBUILD` command for original seated pilot, native action+journal event with existing ship custody, source frame `ONBOARD|1` following original `SHIP`. Commands retain one-use exact native ids and active session. The engine may refuse any preflight at actual execution.
- `desktop_godot/world/native_player_session.gd`: exact request, ship, tick, generation and strict single-frame validation; completed original SHIP events and input receipts remain the only success authority.
- `desktop_godot/world/group3_mining_panel.gd` and `main.gd`: four actual physical onboarding controls, original source quantities, outpost identification and post-docking instructions, with source-backed enablement. Does not grant mining/tool installation.
- `src/desktop_station_host.cpp`: new-world-only finite original deposits and finite original colony life-support; separate `INDRUN` runs exactly 16 native world steps for already-approved original local jobs, verifies mass conservation and reports actual batch change versus stall.
- `desktop_godot/world/native_station_session.gd`, `refinery_terminal_ui.gd`, `main.gd`: guarded, serial `INDRUN` request; exact source stock, worker and job counters; player-facing material/worker/approval progress. DOCK-08's original 28 owner-selected operations are preserved.

## Source and safety invariants

- **No new galaxy-wide account or inventory;** original distinct ship ore, source mineral, ship manufactured modules, original built-outpost source, facility embodied stock, local original warehouse bins, workers, and native output stay distinct and physical.
- **No free outpost:** four paid original modules; the original site is created by an applied kernel command at a physically stationary source hull, not by a Godot popup.
- **No false original output:** `INDRUN` cannot assign an extra worker, approve a program, create material, increase a job target or skip a native STEP. Only actual native counter increments count as manufactured.
- **No invisible starter conversion:** preexisting ordinary station saves cannot use these built-outpost actions without making and docking at a genuine physical outpost. Preexisting saves lacking finite local raw deposits remain unchanged.
- No automatic PR/CI/merge/release action is performed in this development group.

## Staged test (not yet executed)

`tests/Dock09IndustrialVerticalSliceTests.cpp`, CTest `galaxy_dock09_original_colony_industry_vertical`, explicitly stages original physical asteroid mining, ore refinement, module fabrication, four-module outpost construction, physically docked source custody, original habitat/utility/manufacturing construction, finite resource transfers, actual worker training, supervisor appointment and timed shift, one approved batch, 16 native progress ticks, 19 kg genuine panels and mass conservation, owner/remote denial and SQLite restart source equivalence.

**Acceptance remains open until:** native C++ compile + CTest, installed Windows UI and exact-protocol, human physical approach/stop/build/dock, original mining and multi-stage worker/power cycle, workshop source output, restart/recovery, source mass and site provenance, and correction of any demonstrated failure. The staged source code alone cannot prove these outcomes.

## Remaining DOCK-09 risks and follow-up

- Source preflight eligibility is conservative, not a real-world command receipt. If a station/rock is too close to a proposed build site, the original kernel rejects it. Improve physical site placement guidance and inspect all native collision geometry during owner acceptance.
- Full native lifecycle and human acceptance remain deferred. The new build host must be checked for per-action failure at native execution, rendering state after original docking and station/ship writer handoff, full worker shift timing, energy availability and output ledger.
- On existing historical saves with depleted ore or missing physical labor, DOCK-09 does not award resources or reset the world; the existing **New game (archive current world)** is the reviewed explicit alternative.
- Further colony geometry and an industrial production/scenery visualization can be worked on after actual native/installed vertical-slice acceptance. There is no fabricated model of an operating factory.

**Status: DOCK-09 engineering source staged; native/installed acceptance OPEN.**