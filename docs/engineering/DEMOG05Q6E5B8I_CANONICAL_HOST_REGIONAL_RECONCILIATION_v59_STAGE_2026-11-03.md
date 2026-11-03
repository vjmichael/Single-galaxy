# DEMOG-05q6e5b8i — Canonical host + regional shipment reconciliation, v59 staging

**Date:** 2026-11-03. **Project:** Original Single-Galaxy C++17/SQLite game, not EVEmu.
**Disposition:** v59 **focused integration-stage acceptance PASSED**; canonical playable-game merger and production multiplayer verification **OPEN**. GitHub remains a repository and documentation destination, not the build host.

## Critical source-boundary audit

The main branch `CMakeLists.txt` currently builds an older `galaxy_kernel`; it does not compile the v58 shipping/warehouse/production machinery from the independently tested focused-kernel source. Asserting that the earlier 44-test focused suite proves the playable game's logistics would be incorrect. A naive side-by-side link of both independently evolved `galaxy::Simulation` and `PersistentWorld` definitions could violate C++ ODR, diverge SQLite schema/snapshots, and create conflicting inventory authorities. **No main executable or native Windows game-client merge is claimed.**

## v59 implementation (source archive and patch, not merged to main runtime)

1. New `CanonicalShippingHost.hpp/.cpp` compiled façade combines the same live `PersistentWorld`, `RegionalProductionNetwork`, `RegionalModeAuthority`, `CheckpointStore`, `ThirdPartyRegistry` and `ProductionShippingHost`. It checks trusted host principal identity against the submitted command issuer, enforces freight role checks, and does not expose a free material-credit or owner-reassignment operation. Test identity callbacks are **not** production network-session authentication.
2. A prepared cold/detailed transition blocks host-admitted player commands, bookings, dispatch, load, rerouting, voting and delivery. Only the matching durable handoff ID/target can resume after a failed prepare; stale or conflicting retries are refused.
3. New `RegionalShipmentReconciliation.hpp/.cpp` is invoked inside the **same SQLite transaction** as `RegionalModeAuthority::commit` after mandatory factory/network handoff validation. It persists an immutable v59 receipt prefix bound to region scope, authority generation/revision and world snapshot generation. It **never** credits or debits cargo; the canonical world `resource_ledger` remains the physical custody authority.
4. Source-region scoping uses `sg_v53_world_return.scope`. Each witness derives in-transit, original-received, returned-to-origin and alternate-received statuses from cargo movement receipts at its historical cutoff. Later deliveries cannot rewrite an earlier region handoff. Independent region scopes do not inherit another region's freight.
5. `verify_third_party_registry` now also checks v59 historical witnesses on world reopen and recovery: exact-prefix digest/counters, missing/deleted row anchor, stale regional generation, and current regional owner consistency. Consistency witnesses are **not** external cryptographic sealing.

## Executed local validation

| Gate | Result |
|---|---|
| Strict GCC C++17 Release complete CTest | **44/44 passed** |
| Focused v59 Debug UndefinedBehaviorSanitizer | **1/1 passed** |
| Fresh ZIP extraction, separate GCC build (`-O0 -DNDEBUG`), focused test | **1/1 passed** |
| v58→v59 patch dry-run/apply and changed-file byte equality | **9/9 passed** |
| Archive internal SHA-256 inventory | **353/353 passed** |
| ZIP full-file SHA-256 | **verified** |
| Real game executable, Windows binary/client, production multiplayer identities, multi-process regions | **not tested** |

The extended physically backed four-owner test follows actual manufacturing returns, independent carrier cargo, gate travel, two constructed receiving stores, two-party settlements and world restart/replay. It additionally tests in-transit cold→detailed→cold and settled cold→detailed→cold, unrelated-region cargo isolation, injected failed promotion/demotion with exact retry, old command rejection while pending, missing handoff, forged handoff digest, forged regional owner, and prior ledger/settlement corruption. Original production escrow remains 8 kg and total world material is conserved.

## Artifact and source reproducibility

- **Full focused v59 ZIP:** `SingleGalaxy_DEMOG05q6e5b8i_CanonicalHost_RegionalReconciliation_v59.zip`, **1,113,020 bytes**.
- **SHA-256:** `b6c7cf1bb1da727312e97cf0f3e8819ae2d20c2a641d302e98825a89f1384702`.
- **Rebased from verified v58 ZIP:** SHA-256 `5983a9d2d83e8c6232a3c14d30491a85ed5c6f1805b12a4e173fe283752473a4`.
- ZIP contains complete focused source, nine-file v58→v59 patch (`docs/DEMOG05Q6E5B8I_v58_to_v59.patch`), tests, engineering reports, executed Release/UBSan/fresh build logs and a 353-entry `SHA256SUMS.txt`.

## Gate not yet closed: actual canonical-game integration

Unify `Simulation`/`PersistentWorld` and SQLite migration ownership in the canonical game's main executable rather than installing a second runtime. Bind game ingress to genuine session identities and prevent direct access to host-only APIs. Validate multiple production regions under concurrent worker processes and test owner-generation fencing during restarts. Bind constructed machines, worker shifts and utilities, GUI routing, and run native Linux/Windows installed-game playthroughs, including interrupted loading, region promotion and full restart. Treat cargo/reconciliation receipts as authoritative world facts; never double-book cold macro `Cargo`.

**Safety:** dependent fatalities remain blocked and police/fire/institutional EMS remain deferred.
