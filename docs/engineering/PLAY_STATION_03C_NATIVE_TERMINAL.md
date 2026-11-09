# PLAY-STATION-03C — original-world refinery terminal and Godot client

This stage is stacked on #121 (03B original C++/SQLite ore custody), #120 (03A processing), #119 (02 station boarding). It does **not** change default game genesis, grant a ship, place a free refinery, or claim a finished station/asteroid gameplay vertical slice.

## Scope implemented

The station child host now accepts two new bounded commands:
- `REFVIEW|session|request|expected_generation|station_id` — strictly read-only refinery projection. This never processes ore, generates power, or manufactures equipment.
- `REFINE|session|request|expected_generation|lot_id` — request for one actual one-tonne lot in the **original station hangar**. The user cannot specify a ship actor, product list, output quantity, refinery location or command owner. The host derives actor/owner/station from the persisted player and installed refinery.

Authorization requires the original persistent **AtStation** player at the same physical station, within 700 mm of the refinery terminal's authoritative (X=3000 mm, Z=0) location, valid host session, monotonic request number, exact original SQLite world generation and the currently held exclusive native-writer OS lock. The original `Simulation::submit` still decides whether any `RefineStationOre` command may be admitted; `PersistentWorld::step` commits physical stock, utility energy and execution outcome in the same SQLite world. A missing/foreign/unloaded lot is denied. Process/SQLite exceptions terminate the host without sending a false success frame.

The existing station `PERSON|2` walking/boarding/exit protocol is unchanged. For explicit refinery view/execute requests only, the host sends a bounded sequence of read-only original-world `REFHEAD|3`, `REFLOT|3`, `REFMIN|3`, `REFEND|3` frames. `REFEND` includes original command result and actual transferred kg. At most 48 eligible physical lots appear per view; header declares total eligible lots and shown count. Each mineral product comes from the station's committed mineral inventory, not a guessed client recipe.

The Godot `native_station_session.gd` validates the expected session, outstanding request, station custody, row counts, original tick/generation, unique lot IDs, physical taxonomies, mineral identifiers and total mineral kg **before** emitting `refinery_received`. Unexpected/malformed partial frames fail closed; no optimistic client inventory changes. Godot's separate `refinery_terminal_ui.gd` renders this verified snapshot, allows the user to select a real `lot_id`, and submits that ID only. A C++ refusal or missing plant keeps the UI read-only.

The station graybox now has a visual terminal at (3,0). In Godot, H enters the existing native station session; WALK with WASD toward the terminal; R opens/closes the refinery panel; click a specific lot and request refining. A cursor opens for panel interaction. The host, **not** Godot collision geometry, checks the physical terminal range. E still handles hangar boarding. The player's actual registered station position is persisted in SQLite; UI interaction does not move that person.

## Tests executed

Locally, the exact original C++ host and 03B fixture were compiled with the real Single-Galaxy native kernel; **7/7 CTests passed** including cockpit flight/pipe, native station boarding/pipe, 03A physical batch, 03B original-world SQLite recovery and new 03C refinery terminal child-pipe test.

The 03C child test creates a bona fide built/owned station with a real staffed utility, two fabricated modules, tagged ore produced by original ship mining and a committed unload. It verifies remote-terminal denial, physically walking into range, authentic lot projection, one allowed `RefineStationOre` command, 1,000 kg of named station concentrates, replay/empty-lot denial, and **independent SQLite restart/readback**. Full original mass ledger remains the 03B guarantee.

A separate synthetic Godot parser/terminal smoke was added, plus source-only regression. These **do not constitute** an engine runtime pass. A dedicated Windows/Linux GitHub workflow builds the exact original sources under native OS environments and executes Godot 4.7.2 real headless scene/parser tests. A Windows precompiled project ZIP is uploaded only after native process/SQLite + Godot tests pass; **do not treat a queued or failed workflow as compiled user build**.

## Remaining gates and out-of-scope work

- Genuine Windows MSVC test logs/artifacts and installed Godot 4.7.2 UI/camera/input acceptance.
- A complete player-driven mining/return/unloading loop and in-game construction UI that produces a real refinery without using developer fixtures.
- Integration of physical terminal with eventual production station geometry and NPC operations.
- Physical ship-asteroid collision #114 and native equipment-driven visual mining #117.
- Multiple-paged lot listing beyond the first 48 rows (header clearly discloses truncation), richer worker/power management and station product hauling/selling.

The initial simple station test save created by `--explicit-test-station` has **no free refinery or ore**. The terminal honestly displays the missing facility until constructed through real game actions or loaded from a separately, explicitly prepared test world. Do not silently inject material or overwrite a user's persistent save to make a screenshot look complete.
