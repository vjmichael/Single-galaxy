# DEMOG-05q6e5b8c — Two-way constructed-world warehouse custody (v53)

**Date:** 2026-11-03. **Project:** Original Single-Galaxy C++17/SQLite simulation, not EVEmu.
**Status:** Implemented and independently tested in a focused local v53 source bundle. **Not merged into complete game kernel, GUI or native Windows release.** GitHub is repository/documentation, not development host.

## Authoritative finished-goods return

v52 already moves 10 kg of legitimately mined/refined aluminum from a constructed outpost store into a bounded two-machine network's physical inventory escrow. v53 adds a host-only `HostReturnConstructedStock` command and privileged `RegionalProductionNetwork::return_finished_to_constructed_storage` operation. One SQLite transaction withdraws the exact grams of *previously manufactured* plates or frames from the network warehouse, reduces the original outpost's `network_escrow_kg`, and credits its real available goods with canonical `aluminum_panels` or `aluminum_frames`. Whole kilograms only; no fractional-unit rounding, unbacked materials, duplicate receipts or full storage.

The same committed operation contains the versioned world tick/snapshot, `resource_ledger` return endpoints, immutable `sg_v53_world_return` receipt, network checkpoint, and independently replayable `sg_v51_event` effect. A missing factory-debit witness causes the world transaction to roll back. Ordinary `PersistentWorld::step` cannot bypass the network settlement, which is a private friend-only interface. The original birth/ferry/industry action ordinals and old snapshots remain readable; new worlds use `galaxy-kernel-v53`.

World reopen cross-checks the applied command count with returned goods/industry receipts. Network reopen deterministically replays machine production, transfers, world returns, promotion and demotion; verifies material identifiers, original constructed site, source ship, world command and generation; and enforces `original reserved mass - returned mass = remaining world escrow = remaining factory mass`. These checks reject missing and altered evidence; they are not external cryptographic tamper-proof sealing.

## Actual test scenario

Six NPCs in two disjoint ART02 family scopes, a real constructed outpost store, 10 kg of mined/refined aluminum, one press and one frame assembler. Two complete production batches generate frame goods. The host returns **1 kg aluminum frames while Cold**, then promotes to Detailed, produces more aluminum panels, returns **1 kg aluminum panels while Detailed**, and demotes without duplicating work in progress or any stock. The world store now has 1 kg of each finished good and retains **8 kg** in escrow. World `total_material_kg()` is conserved.

Tests cover insufficient finished stock, unauthorized material code, duplicate event/command ID, interruptions after network payload/effect/world snapshot, exact retry, recovered-world snapshots, independent genesis journal replay, deleted immutable factory return receipt and forged world resource-ledger destination. The older 39 tests remain passing. The next item is distribution into real ship cargo and other constructed sites, not claimed in v53.

## Reproducible results

| Verification | Result |
|---|---|
| Strict GCC C++17 Release full CTest | **40/40 pass** |
| GCC Debug UndefinedBehaviorSanitizer full CTest | **40/40 pass** |
| Independent copied source, fresh Release build and full CTest | **40/40 pass** |
| Pristine v52→v53 patch dry-run and apply | **Pass; 7/7 changed files byte-identical** |
| Independent build vs packaged source definitions | **98/98 byte-identical** |
| Final ZIP fresh extraction and SHA-256SUMS | **296/296 matched** |

**Artifact:** `SingleGalaxy_DEMOG05q6e5b8c_TwoWay_Warehouse_v53.zip` (**927,348 bytes**), complete focused C++17/SQLite source, 40 tests, seven-file v52→v53 patch, regression/build logs, engineering report and per-file SHA256SUMS.
**Artifact SHA-256:** `b7fd057f3f5b877cf89da2fda6304c2b32320aad9d5a06d0209e3544fe30361c`.

## Explicit remaining boundaries

- The world material taxonomy is deliberately limited to whole-kg plates and frames; the network still has a two-to-eight machine cap and one constructed source warehouse. Finite intersite shipping, real cargo delivery, distributed industry, real constructed machine identity and utility/shift integration remain open.
- The test does not establish a successful warehouse-to-ship finished-goods transfer or a player-facing sales/market UI. The complete game has not been merged or native Windows tested.
- Dependent ferry fatalities remain blocked; **never depict children being harmed or dying**. Full police/fire/institutional EMS remain deferred.

**Next:** DEMOG-05q6e5b8d — authoritative multi-location warehouse-to-cargo finished-goods deliveries, material-code/capacity admission, durable haulage and constructed machine bindings.