# Single Galaxy — B7.0e: Player-Built Greenhouses and NPC-Staffed Agriculture

**Date:** 2026-10-30  
**Status:** Implemented and Linux-tested core vertical slice. **Not** a complete autonomous colony, NPC society, or finished graphical release.

## Why this milestone

B7.0d had finite provisions and manual shipboard hydroponics. B7.0e gives agriculture an actual **player-built location, physical inputs, finite workers, production schedule, and transport constraint**. Settlements can produce useful goods without repetitive planting clicks, but cannot conjure supplies. This is one step toward **stability → livability → self-sufficiency**, not an arbitrary technology-tier unlock.

## Implemented and verified

1. **No initial civilization.** The colony scenario still starts with one ship, 24 founding NPCs, bounded 2D space, asteroids, and **no stations or gates**. Legacy prebuilt infrastructure remains exclusively in historical test scenarios.
2. **Build on a player-created outpost.** `ConstructFarm` installs a greenhouse only at an existing outpost created by the same colony ship/owner. It consumes **two real construction modules (four tonnes embodied)**. Duplicate, foreign, remote, or unsupported construction is rejected.
3. **Station-local stores.** `TransferFarmStock` transfers food, water, oxygen, nutrients or captured CO₂, in whole kilograms. The ship must actually be within **2,500 mm** of the outpost. Negative amounts retrieve goods. The farm's cargo does not teleport into the ship when the captain flies away.
4. **Finite NPC assignment.** `AssignFarmWorkers` moves 1–8 crew members between the colony ship and the outpost; there are no spawned workers. At least one ship pilot remains. The destination must have three cycles' food, water and oxygen for the stationed crew before assignment.
5. **Automated farming, not free farming.** Staff plant if their local inventory permits: 2 kg water + 1 kg nutrients + 1 kg CO₂ → 3 kg growing biomass + 1 kg oxygen. After five ticks, biomass → 2 kg food + 1 kg compost. Up to eight workers plant in parallel. Compost and local wastewater can be processed; recovery produces real sludge. Local workers consume food/water/oxygen each 20 ticks and produce waste and CO₂.
6. **Management is physically constrained.** The outpost continues working after the ship travels beyond supply-transfer range **while the server simulation is running**. When inputs run out, farming stops. Staff can become undersupplied; no magical recovery, staffing or growth.
7. **Explainable viability.** The assessment reports current total colony population, farm staffing, and food production versus demand during the *last three completed life-support periods*. Each period—not just the average—must cover crew demand to qualify as a demonstrated food-production surplus. **This does not certify Livable/Self-Sufficient/Prosperous**, because water renewal, energy, amenities, maintenance, and actual independent settlement operations are incomplete. Expansion readiness remains false.
8. **Durable and auditable.** Snapshot `galaxy-kernel-v11` and the existing SQLite transaction/command journal preserve farm location, staff, inventory, crop timers, production records, and resulting mass transfers. Construction and delivery/collection receipts are checked independently of the world snapshot. Older snapshot versions and milestone tests remain supported.
9. **Local UI.** The authenticated Matrix Vision colony panel displays outpost and farm telemetry, farm staffing, recent food output, and supply conditions, and accepts build, stock, assignment and recall commands through the origin/token/schema-checked local relay.

## Test evidence

- **35/35 CTest suites** have passing Linux results across bounded tests: 1–10, 11 isolated, 12–21, 22–35. The initial all-at-once run stalled in old live scanner test 11 and is **not counted**; isolated test 11 passed.
- **58/58 JavaScript tests** passed, including four new farm telemetry, malformed-input and UI wiring checks.
- New native B7.0e test: resource conservation; wrong-owner denial; failed and successful staff assignment; local transport; standing harvest cycles; ship departure without remote resource teleportation; food-supply demand check; SQLite reopen and journal recovery copy.
- New live B7.0e test: **actual host** executes build and farm commands, receives production telemetry, reloads its SQLite save; **relay** rejects forged/malformed/unauthenticated/cross-origin calls.
- `galaxy_colony_live_host`, `galaxy_kernel` built and ran on Linux.

## Limits / follow-on work

- Not yet a full **autonomous colony**: no farm energy budget or machinery breakdown, no individual NPC identities or happiness, no housing/services, no agriculture beyond the server's active runtime, no independent legal/economic government, and no NPC-controlled expansion.
- Plants use **simplified game-balanced matter conversions**, not laboratory biochemistry. Waste is retained; no free mass.
- Work scheduling is deterministic, with aggregate staffing. No player autopilot or continuous interstellar 3D movement yet.
- Bounded 2D Linux verification only. **Windows-native build and real Chromium graphic click-through remain unverified.**

## Next work

A viable follow-on is **B7.0f — Outpost habitation and provisioning**: physical crew quarters, water/oxygen/food allocation for residents, repeatable local logistics and a measurable quality-of-life foundation. The automated administration/managed-absence layer needs these before it can responsibly continue expanding a settlement. Later introduce power, maintenance, independent supply contracts, individual NPC welfare, and social institutions.

## Commands and source

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R b70e --output-on-failure
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

Under the local colony panel: build the first outpost from modules, select that outpost in the B7.0e greenhouse panel, build the farm, deliver food/water/oxygen/nutrients/CO₂ and assign at least one colonist. Retrieve food by transferring a negative quantity near the outpost. The production runs on the server thereafter until inputs are depleted.
