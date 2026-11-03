# DEMOG-05q6e5b8e — v55 Durable Shipment Manifests (verified focused kernel)

**Date:** November 3, 2026. **Project:** Original Single-Galaxy C++17/SQLite simulation, not EVEmu. **Status:** Implemented and locally verified in a focused source archive; **NOT** merged into the playable game, canonical main-branch runtime or native Windows application.

## Changes

- New `ShipmentManifest.hpp/.cpp` host registry: immutable named, single-item-code booking, one factory-return witness, whole-kg amount, physical origin warehouse, carrier ship, authorized pilot, destination system, and unique load/unload command IDs.
- An immutable destination-site designation after the same ship physically reaches another system and constructs its destination outpost warehouse. No instant stock delivery.
- SQLite `sg_v55_manifest`, `sg_v55_destination` and an atomically maintained count in `world_meta.shipment_manifest_count`; immutable triggers and replay detection for absent, altered or duplicated manifests. Not claimed as external cryptographic protection.
- `PersistentWorld::submit` reservation admission plus a repeated check under the canonical SQLite world commit writer lock. Includes alternate storage-node and workshop cargo routes, preventing already-queued outsider commands from bypassing bookings.
- Real world transfer preflight for item, ship, pilot, site ownership, jump state, proximity, finite volume and available stock. Loading/unloading drive canonical world `TransferStorageStack` and commit receipts. Interrupted steps retry the same accepted command identity; no shadow inventory mutation.
- Recovery verifies v53 factory-return provenance, world resource-ledger endpoints, historical action witnesses, completed jump chronology, constructed destination, reserved cargo stack isolation, world metadata count and delivery journal.

## Frozen acceptance scenario

Factory return `9102` (1 kg aluminum frames) and return `9106` (1 kg aluminum panels) are separately booked. The actual colony ship loads via commands `9201`/`9202`, jumps from system 1 to system 2, creates a second materially constructed store, designates its destination, and unloads via `9206`/`9207`. The original 8 kg production escrow remains, mass is conserved, and completed deliveries remain after SQLite reopen and independent genesis recovery.

Negative cases: duplicate factory source/manifest command identities, unreserved transfer, prequeued cargo transfer, secondary storage-node bypass, unauthorized ship use, rejected overdraw, cross-system remote transfer, two injected interruption points for loading and unloading with exact retry, repeated destination designation, deletion of one manifest, deletion of the entire registry, deletion/forgery of destination-site designation, forged cargo or world resource-ledger endpoints.

## Local verification

| Gate | Result |
|---|---|
| Strict GCC C++17 Release, full serial CTest | **41 / 41 passed** |
| GCC Debug UndefinedBehaviorSanitizer, v55 focused | **1 / 1 passed** |
| Fresh extracted bundle, C++17 build and v55 test | **1 / 1 passed** |
| Inside ZIP per-file SHA-256 verification | **313 / 313 passed** |
| Pristine v54→v55 patch dry run and applied-file byte comparison | **6 / 6 passed** |
| Native Windows / playable game / multi-owner hauling | **NOT TESTED / NOT IMPLEMENTED** |

**Source archive:** `SingleGalaxy_DEMOG05q6e5b8e_ShipmentManifests_v55.zip`, **981,655 bytes**, SHA-256 `8bdf266e7bc02f9070f3587c5363988d168fe4438e83325e945a8c8d7358c3bb`. Archive has full focused source, immutable receipt registry implementation, CTest, v54→v55 six-file patch, engineering report, checksum manifest and native build/CTest logs. Baseline v54 SHA-256: `080180c84b41cc36d0c4ee19186f24b2953829be7d97346cf2dff6c7f4bdb208`.

**Integration boundary:** This module is still a focused C++17/SQLite local feature, not merged into the main game. Carrier and destination source ship share owner; current `TransferStorageStack` enforces source-ship ownership. General third-party shipping, independent warehouse owners, delegated grants, rescinded grants, split-lot recovery, market transfer, cross-region dispatch, real constructed machine/workforce/utility binding and native Windows QA are future work. v55 retains inherited v53 world snapshot serialization, with companion SQLite shipping tables.

**Next recommended work:** `DEMOG-05q6e5b8f` — independent carrier/warehouse ownership and authenticated two-party handover, finite cargo and scoped delegated permissions, committed delivery/settlement receipts, cold/detailed regional reconciliation, and negative replay/auth tests. Do not bypass the physical world custody rules to shortcut third-party delivery.

Dependent fatalities remain blocked; no depiction of children harmed or dying. Police/fire/institutional EMS remain deferred.
