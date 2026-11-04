# PLAY-09 — Physical Wreck Salvage and Home Return

**November 4, 2026 · Branch `integration/play09-physical-wreck-salvage-20261104`**

## Design boundaries
PLAY-09 adds the first player-controlled *physical wreck recovery* transaction to the authoritative C++/SQLite Single Galaxy world, extending PLAY-08's uncrewed combat-training wreck and PLAY-04's bulk-ore cargo/docking.

The temporary 12-charge training weapon is **not** the approved final ammunition mechanic. A proper weapon and ammunition/fuel/energy production-and-logistics system is deferred. PLAY-09 deliberately makes no weapon balancing, crafting, ammunition generation, or firing modifications beyond inheriting the training fixture.

## Closed-loop gameplay
1. The player starts with one finite-crew colony ship; the training drone is an uncrewed, non-colony target.
2. At genesis, the drone receives **six physically pre-existing tonnes of bulk recoverable material** stored in its own cargo hold. This mass is included in `total_ore()` from the beginning. Destruction never creates loot, ammo, or resources.
3. After the drone is destroyed through the previously implemented real combat command, its existing entity becomes a persistent `Wreck`, retaining the same ID, world position, owner and six-tonne stock.
4. The player pilots within **2,500 mm**, selects the native-reported wreck, and issues authenticated `colonySalvageWreck`. The relay accepts only target ID and integer amount 1–10; the native host checks actor identity, world coordinates, dock/jump state, target type and owner.
5. The C++ `SalvageWreck` command transfers at most the requested mass, existing wreck cargo, and the ship's genuine free cargo space. The exact atomic move is `actor.cargo += moved; wreck.cargo -= moved` in the same world journal step. Neither ore nor ownership teleports.
6. The player can return and unload through PLAY-04's already implemented local outpost cargo pipeline, then refine, fabricate, or recycle the recovered bulk material under the existing resource rules.

## Safeguards and non-goals
This **first pass** only permits recovering stock from owner-8 practice-drone wrecks, not any arbitrary player ship, colony ship, populated ferry, civilian vessel or infrastructure. Wrecks do not disappear when empty; future salvage and recycling can model physical hull breakdown separately. Recovering finite preexisting cargo is **not yet** cutting up ship structure into manufacturing alloys or manufacturing ammunition.

Native result outcomes include `InvalidTarget`, `Unauthorized`, `OutOfRange`, `NotInSpace`, `InTransition`, `NoResources`, and `CargoFull`. The browser is a view and command issuer, never an inventory writer. A 202 response is only queued, not confirmation. No player avatar/boarding decision, NPC immigration, or general combat AI is introduced.

## Tests and acceptance
- `tests/PLAY09_WreckSalvageTests.cpp`: create two source-stock drones, fire to wreck one, reject intact and unauthorized salvage, transfer 4 t and later final 2 t, reject depletion, verify total material unchanged throughout and across SQLite reload and recovery-copy.
- `tests/PLAY09_SalvageWiringTests.js`: validate strict telemetry parser and exact relay/UI/native path.
- `.github/workflows/play09.yml`: build native C++ game and live host; run PLAY-08 and PLAY-09 native tests plus existing browser regressions.

**Status:** implementation is committed to a draft PR. Current-head CI and a genuine Chromium/Windows live playthrough are separate acceptance gates; neither should be claimed complete without observed results.
