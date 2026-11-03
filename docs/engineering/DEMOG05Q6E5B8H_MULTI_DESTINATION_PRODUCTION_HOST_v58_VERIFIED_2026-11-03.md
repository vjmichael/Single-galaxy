# DEMOG-05q6e5b8h — Multi-destination routing and production-host integration (v58)

**Date:** 2026-11-03. **Project:** Original Single-Galaxy C++17/SQLite simulation, not EVEmu. **Status:** Focused-kernel implementation locally verified; *not* merged into canonical game executable, multiplayer production host or native Windows release. GitHub is the documentation/repository destination, not build host.

## What v58 implements

- A loaded, manufactured-goods consignment may be redirected exactly once from its independently owned original receiving warehouse to a fourth owner's separately constructed destination warehouse. The source owner (7), carrier owner (8), original receiver (9) and replacement receiver (10) approve through independent host-authenticated role checks.
- Immutable SQLite `sg_v58_alternate`, `sg_v58_alternate_approval`, `sg_v58_vote`, and `sg_v58_settlement` tables carry the destination identity, reserved world command ID, four consents, canonical physical transfer receipt and two-party carrier/replacement receiver settlement. World metadata count anchors detect removed rows; these are not externally cryptographic seals.
- Alternate routing disables the original destination unload and prevents a conflicting return-to-origin plan. The existing host-only `HostTransferConsignedStock` action preserves physical cargo, finite storage, host authorization, physical inter-system jump and real local proximity. The replacement owner receives 1 kg frames and original receiver retains its 1 kg panels. Carrier cargo becomes empty and total world mass plus 8 kg network escrow are conserved.
- In-process `ProductionShippingHost` binds the real `RegionalProductionNetwork` verifier and authoritative world factory-return evidence to shipping booking, authenticated dispatch and redirected delivery. Bookings without legitimate material codes, factory-return receipts, or available world warehouse stock are rejected. This is an integration adapter within the focused kernel, not a real network server or UI.

## Verification actually run

| Gate | Result |
|---|---|
| Strict GCC C++17 Release full CTest | **44/44 passed** |
| Focused Debug UndefinedBehaviorSanitizer CTest | **1/1 passed** |
| Fresh ZIP extraction, separate rebuild (`-O0 -DNDEBUG`) and focused CTest | **1/1 passed** |
| Internal archive SHA-256 matches | **341/341** |
| v57→v58 patch dry-run/application and per-source SHA-256 comparison | **4/4 files byte-identical** |

Focused test exercises a real four-owner factory-to-cargo-to-warehouse handoff, separate original receiver cargo, four-way consent, refusal of unsigned/repeated consent, original destination bypass denial, refusal before a physical jump, actual alternate approach, interrupted ledger/snapshot writes followed by exact retry, immutable settlement, world restart, independent genesis recovery, and refusal of forged/deleted route, approval, ledger and settlement evidence. Previous 43 tests pass unchanged.

## Reproducible focused-source deliverable

**ZIP:** `SingleGalaxy_DEMOG05q6e5b8h_MultiDestinationProductionHost_v58.zip`; **1,081,146 bytes**. SHA-256: `5983a9d2d83e8c6232a3c14d30491a85ed5c6f1805b12a4e173fe283752473a4`. Contains complete focused C++17/SQLite source, 44 tests, v57→v58 patch, reports, build/test logs, fresh-source verification and `SHA256SUMS.txt`. The ZIP is preserved as the conversation/Library artifact, not checked into this main branch.

**Baseline v57 ZIP SHA-256:** `77dafcef01fc48b57ceac89102f5c96894a4b715128da061bfab955175fe7e4f`.

## Deployment and scope exclusions

The new route permits *one* replacement site per consignment, not arbitrary multiple stops, further changes to the replacement, split lots, or financial-market settlement. Live multiplayer sessions, actual host authentication, multiple concurrent region processes, distributed cold/detailed production scheduling, binding of physical machines/staff/utilities to shipping, game-client merge and native Windows acceptance remain unverified/open. Do not interpret the focused adapter as production-host deployment.

Dependent fatalities remain blocked; children must never be visually depicted being harmed or dying. Police/fire/institutional EMS remain deferred.

**Next logical gate:** DEMOG-05q6e5b8i — canonical game-host merger and warm/cold regional shipment replay, with real authenticated ingress and Windows acceptance.