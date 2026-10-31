# Single Galaxy — B7.0l Mineral Water Replenishment and Local Waterworks

**Date:** October 31, 2026  
**Status:** Implemented and Linux-verified; Windows-native and graphical Chromium playthrough not verified.  
**Parent release:** B7.0k-2 local water/oxygen reclamation.

## Engineering goal and scope

Supply a player-founded colony with *additional* drinkable water from a real finite mineral deposit, rather than mistaking wastewater recycling for net renewal. Preserve the one-ship bootstrap, physical transfer ranges, exact material ledger, and conservative managed-absence progression. The stage remains a bounded deterministic **2D** simulation, not the intended continuous 3D galaxy.

## Implemented end-to-end chain

1. **Geological extraction:** Existing B7.0c carbonaceous geology yields finite `serpentine` and `saponite` when a colony ship physically mines its asteroid. Existing `RefineMineral` separates water-bearing mineral concentrate into **raw `goods_kg["water"]`** plus ceramic and retained tailings. This uses the established game-specific 10%-water separation yield for those phases; it is not a real-world purification model.
2. **Local waterworks:** Authorized colony ships build at most one waterworks at their own nearby outpost, after constructing a habitat and solar utility. Cost: **two manufactured structural modules = 4 tonnes embodied mass**. No structure exists at genesis.
3. **Sealed transfer:** `TransferWaterworksFeed` moves **positive or negative multiples of 1,000 kg**, up to 10,000 kg per action, between ship-held raw water and the locally constructed facility. The ship must be inside the existing 2,500 mm physical docking/transfer radius, in the same system, and authorized by the owner. Retrieval back to the ship checks capacity. These are sealed-batch logistics rules; **separate modeled tank objects are not yet implemented**.
4. **Power-dependent purification:** Every 20-tick needs cycle, a supplied, occupied, powered waterworks processes up to **20 kg of raw feed** using the game recipe **10 kg raw feed → 9 kg potable water + 1 kg brine**. The potable portion is physically distributed to the co-located habitat and, for working greenhouse staff, to the greenhouse compartment. Brine stays at the waterworks. Powered processing adds one unit of local game electricity/tick when there is sufficient feed and occupied demand. Lack of power or feed stops production; empty/idle plants do not create demand or supplies.
5. **Conservative diagnostics:** `assess_water_supply()` checks **every occupied outpost**, measured net potable output versus local water demand over three periods, and three consecutive powered supply periods. It exposes a *short-window coverage* flag; **`indefinitely_renewable` is always false** because geological deposits are finite and there is no unlimited source or self-operating procurement.
6. **Interface and security:** New `WATERWORKS` and `WATER_SUPPLY` telemetry frames retain older `RECLAIMER`, `LIFE_RECOVERY` and `RESILIENCE` contracts unchanged. Local Matrix Vision controls send only the two authorized commands `colonyWaterworksBuild` and `colonyWaterworksFeed` through the exact-schema origin/session-token validated relay. No server grants, admin commands or autonomous-growth controls were added.
7. **Persistence:** New **SQLite canonical snapshot v18** serializes the waterworks, on-site mass inventories, three-period history and pending commands; all v1–v17 snapshots remain readable. An existing v17 world is upgraded only by a new v18 operation, not rewritten just by opening its save. `resource_ledger` now emits **`waterworks_construction_t`** and **`waterworks_feed_transfer_kg`**, verified against executed authoritative events and independent journal command records, including exact source/destination, amount and tick. Tampered receipts reject recovery.

## Tested behavior

- Real geological mining → water-bearing mineral separation → ship goods → sealed outpost delivery → powered 90%-yield purification → locally consumed potable water.
- Workers and residential occupants share the **same outpost only**, not magically connected outposts. Greenhouse staffing with a scarce initial water buffer confirms local line delivery. A delivery to another site's inventory is not inferred.
- Missing prerequisites, duplicate construction, forged ownership, invalid quantities, origin/token/schema bypasses, and out-of-range water transfers reject.
- Site production stops when power or feed is unavailable; no material or crew are created. Exact world mass conservation holds across production, returnable material, and wastewater.
- Read-only waterworks assessment preserves explicit `indefinitely_renewable=false`, independent of a strong short-window output result.
- v18 pending command restoration, canonical snapshot round-trip, separately copied SQLite recovery, intentionally forged plant mass record rejection, and intentionally modified resource receipt detection.

## Linux verification (completed 2026-10-31)

| Gate | Verified result |
|---|---|
| New B7.0l native kernel test | Passed |
| New B7.0l C++ host + local HTTP-relay test | Passed |
| New B7.0l JavaScript contract tests | 3/3 passed |
| Complete JavaScript tests | **81/81 passed** |
| Native/integration regression CTest | **51/51 passed across bounded parallel groups**: 1–12, 13–24, 25–36, 37–51 (not one uninterrupted monolithic run) |
| Production-style Linux compilation | Passed with `BUILD_TESTING=OFF`, `SG_QA_AUTOMATION=OFF` |
| Clean source archive extraction/rebuild | **Passed**: exact SHA-256 per-source manifest, fresh extraction, rebuilt native kernel/live host tests **2/2**, and fresh JavaScript contract tests **3/3**; see `docs/B70l_Clean_*` logs |

## Remaining work and non-claims

- **This does not establish indefinite supply.** Mineral reservoirs deplete, vehicles must still obtain and transport feed, the plant loses 10% to brine, and solar equipment itself wears out. Short-window throughput is not a closed-loop resource certificate; autonomous settlement growth remains blocked.
- The older B7.0d **`LIFE_WATER` command remains operational for backward compatibility** and can still transfer old raw-separated water directly to *shipboard* life-support tanks. The new local purification path does not silently reinterpret historical saves or retroactively invalidate that shortcut. A future explicit migration needs to distinguish untreated mineral water from potable water in all inventories.
- Local waterworks distribution is a fixed co-located site connection, not a full player-built network of pipes, pumps, filtration consumables, sealed container objects, engineering physics, valve permissions, or travel pathfinding.
- No external infinite aquifer, natural water replenishment, steam power, full electrolysis/hydrogen management, microbial contamination, individualized illnesses, governance, off-server catch-up, meaningful NPC trade network, real Chromium clicks, Windows-native runtime, or continuous 3D space.
- Balance constants (20kg/cycle throughput, 90% yield and 1kWh-equivalent unit/tick) are **game placeholders**, not empirical chemical-engineering claims.

## Reproduce locally

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R '^galaxy_b70l_' --output-on-failure
node --test tests/*.js
cmake -S . -B production -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF
cmake --build production -j4
```

For live gameplay launch `scripts/run_colony_linux.sh build/galaxy_colony_live_host` and use the B7.0l panel. All actions are local and save-backed; browser controls do not constitute actual Chromium click-through testing.

**Next gate: B7.0m** — long-window reliability, physical replacement of brine losses, filtration-consumable production, sustainable oxygen/water chemistry, NPC logistics under standing policies, and then cautious managed absence. Do **not** remove `WaterRenewalUnverified` or enable autonomous growth without real evidence and necessary services.
