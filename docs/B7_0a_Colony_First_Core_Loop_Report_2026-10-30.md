# B7.0a — Colony-First Core Loop: Engineering Report

**Date:** October 30, 2026 (user-local)  
**Environment tested:** Linux x86-64, C++17, CMake/Ninja, SQLite system SDK, Python 3 and Node.js.  
**Status:** Implemented and verified as a **headless local vertical slice**. It is not yet a graphical or continuous 3D space game.

## Design priority / scope

The goal is to replace a ready-made station-and-gate opening with the smallest playable **one-colony-ship foundation**. NPC factions, romance, reproduction, autonomous economies, and large-scale social simulation are explicitly deferred. Existing B6.6 Lumen Reach infrastructure is kept only for regression fixtures; it is **not the released game's starting universe**.

### Implemented path

1. New local game world starts with **one colony ship** (owner 7), **24 finite crew** and a cargo capacity of 60 ore-equivalent units. There are **no prebuilt stations, markets, gates, refineries or outposts**. The onboard refinery and fabricator are basic capabilities of the colony ship, not external structures. Three finite asteroid test deposits are the only other entities.
2. `scan` reveals nearby asteroids/outposts within a bounded 25,000 mm local radius. At least one deposit starts outside sensor range; approaching it with actual thrust brings it into view.
3. Real mining (`Action::Mine`) transfers ore from a physical asteroid to the actual colony ship, honoring distance, ownership and limited hold capacity.
4. `Action::RefineOre`: **1 raw ore → 1 refined unit**. `Action::FabricateModule`: **2 refined → 1 construction module**. These simple recipes are placeholders, not scientific metallurgy.
5. `Action::ConstructOutpost`: four construction modules (8 ore equivalents) create a **physical, player-owned outpost/station entity** near a stopped colony ship. Construction requires at least three crew and is rejected for overlapping stations, insufficient material, moving ships and unauthorized actors. The outpost has a starter hangar/docking reference, but **does not automatically create a market, refinery, workforce, or stargate**.
6. Cargo, refined goods, modules and embodied outpost material all count toward a conserved `total_ore()` equivalent. A full hold cannot be bypassed by refining or fabricating. The SQLite world origin and every generation enforce the same total.
7. The existing `PersistentWorld` transactional command/receipt/journal/recovery pipeline now understands the three new player actions, with verifiable second-ledger entries for refinement, fabrication, and construction. Snapshot format **v7** preserves v1–v6 loading.
8. Developer-only interactive terminal harness `galaxy_colony_cli` accepts typed player actions, maintains a SQLite save and resumes from it after restarting.

### Tested gameplay transcript (abridged)

```text
NEW_COLONY one colony ship, 24 colonists, no prebuilt infrastructure
SCAN radius=25000 mm
  asteroid=2 ore=40
mine 2 8        -> APPLIED, raw_ore=8, total_ore_equivalent=170
refine 8        -> APPLIED, refined=8, total_ore_equivalent=170
fabricate 4     -> APPLIED, modules=4, total_ore_equivalent=170
build           -> APPLIED, outposts=1, total_ore_equivalent=170
RESUMED         -> outposts=1, outpost id 5 remains owned by player 7
```

## Verification evidence

- **27/27 CTest native and integration suites passed**, serial in three bounded groups (14/14, 6/6, 7/7). See `B70_ctest_1_14.txt`, `B70_ctest_15_20.txt`, `B70_ctest_21_27.txt`.
- **47/47 JavaScript tests passed**, `B70_node_test_output.txt`.
- New dedicated `galaxy_b70_colony_core_tests` passed (owner denial, finite crew prerequisites, overloaded cargo avoidance, raw/refined/module/station mass accounting, overlap denial, no free market/gates, save/restored state, journal recovery-copy, SQLite rollback fault injection and tampered snapshot rejection).
- New `galaxy_b70_colony_cli_e2e` passed (real stdin commands and process restart, SQLite second-ledger verification, distant-resource scan after physical travel).
- A **production-style build with `-DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF` compiled successfully**; CLI smoke path passed.
- A clean ZIP-extraction build and test result is recorded separately at packaging.

## Limitations and next priorities

1. **Not continuous 3D yet.** The existing kernel is bounded 2D local integer-mm coordinates, with legacy discrete system partitions. B7.0a targets the playable bootstrap first. Hierarchical 3D coordinates, seamless streaming, realistic interstellar scale and direct flight between stars remain major B7 work.
2. **No graphical colony-start UI yet.** The B6.6 browser still loads the legacy test world, not this colony-start path. Next priority: connect the colony-start world to a proper visual desktop/browser test client, display crew/resources/construction, and allow direct flight and building.
3. The 24 crew are currently a **persisted headcount**, not separately simulated identities. No NPC recruitment, mortality, reproduction, families, factions or automated staffing have been implemented.
4. Recipes are instantaneous simplified unit transformations with no power budget, heat, time, waste, by-products or actual module inventories as independent world entities. Initial ship/refinery manufacturing is granted as ship equipment; the outpost has no fabrication modules until later implemented.
5. The local CLI is a **developer test harness**, not the promised no-CLI player experience, multiplayer server, Windows-verified executable, or actual GUI test.
6. No asteroid field generation from real astronomy data yet. Starting rocks are finite manually placed local test fixtures, not realistic ore tonnage.
7. Ship/crew destruction and loss of colony equipment are future work. The intended high-stakes failure model is not claimed as completed.
8. No true player autopilot: QA automation remains separate and disabled for production-style builds.

## Next practical milestones

- **B7.0b:** launch the colony-first scenario through visual client controls; scene receives actual scanned information, raw/refined/components/crew readouts, and construction commands through authoritative relay.
- **B7.1:** persistent 3D spatial coordinates and streaming sectors for a **gate-free continuous universe**. Direct travel between two placed stars with uninterrupted ship identity, inventory and recoverable motion.
- **B7.2:** realistic resource and material processing with construction time, labor, power, real inventories, and first viable station upgrades.
- **Later:** NPC identities/mentorship, player-made gates, trade, combat, faction politics, romance and population growth.

## Build and demo

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j1 --output-on-failure
node --test tests/*.js
./build/galaxy_colony_cli /tmp/my_colony.sqlite
# Enter: scan; mine 2 8; refine 8; fabricate 4; build; status; quit
# Re-run with same file to resume.
```

**Security:** Never accept an arbitrary player-supplied issuer identity in production; this harness uses one hard-coded local identity for repeatable tests. Multi-user authorization and authoritative network ingress remain unimplemented.
