# ECON-03c6 — Region interest and physical-custody manifest (2026-11-01)

**Status:** Locally completed as an isolated C++17 module, **not integrated with the playable `PersistentWorld`**, not merged to `main`. GitHub is source/milestone archive, not the development/test environment.

## Changes
1. `RegionalInterestManager`: finite 3D meter coordinates, distance-to-region **surface**, adjustable enter/exit radii and hysteresis dwell. Multiple player observers keep a region detailed if any is nearby. A transition is **planned only**, and the authoritative host must explicitly acknowledge after a durable simulation handoff. Test defaults: detail at ≤2,000m, cold at ≥3,000m from region surface; values are not final game tuning.
2. `CustodyLedger`: opt-in SQLite manifest tracks verified-host-supplied cross-colony carrier handoff IDs through Reserved → InTransit → Delivered. Enforces unique handoff IDs per scope and matches cargo, endpoint and timestamp values against the scheduler. Shipment custody records, command receipt, scheduler checkpoint all commit in **one SQLite transaction**, using the same underlying database.
3. Migration guards prevent legacy direct checkpoint saves immediately after enabling custody; reject untracked legacy outstanding shipments, wrong SQLite databases/scopes, duplicated cargo bookings, stale writes and unreceipted host command paths.
4. A cargo manifest is **not independent spendable inventory** and does not itself prove NPC work, route/carrier authenticity, or live-world custody. Those require authoritative `PersistentWorld` integration.

## Verification
Locally passed 5/5 CTest Release and 5/5 AddressSanitizer + UndefinedBehaviorSanitizer tests. C6 coverage: hysteresis/dwell, multi-observer safety, command idempotency/restart, cargo conservation, injection of SQL errors during reservation and delivery with whole-transaction rollback, scope/database mismatch rejection and legacy-cargo opt-in safety. Patch also checked to apply cleanly to the exact isolated ECON-03c5 source baseline.

## Deliverables (produced locally; not committed as source on this archival branch)
- `SingleGalaxy_ECON03c6_Regional_Interest_Custody_Isolated_Source_Tests_2026-11-01.zip`
  - SHA-256: `d7f3685219562485eaa3ffbcdad4228c22d35c34df5dc4e82488d66087af6726`
- `SingleGalaxy_ECON03c6_Distance_Interest_And_Custody_2026-11-01_Changes.diff`
  - SHA-256: `3f2fca4510b3d46073cbefb94e2d24dab82a25280480c0b856722575c5487c33`
- `ECON03c6_Distance_Interest_And_Custody_Engineering_Report_2026-11-01.md`
  - SHA-256: `359c86f91ca675355be4c30132af648eac04a4dd9972e5c5dda482966a486680`

## Next step
Wire actual `PersistentWorld` SQLite inventory and host-attested NPC carrier receipts to the same authoritative custody journal, then implement durable active/cold simulation handoff before switching detailed simulation. Keep the 3D visual streaming layer separate from persistent world truth. Unresolved: full game/JS tests, Windows UI, actual rendering asset streaming, route disruptions, multi-player network boundaries and consumer-hardware whole-world benchmark.
