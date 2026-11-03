# DEMOG-05q6e5b8f — Independent carrier and warehouse custody (v56 focused acceptance)

**Date:** 2026-11-03. **Project:** Original Single-Galaxy C++17/SQLite simulation (not EVEmu).  
**Status:** Implemented and independently tested in a complete focused local v56 source bundle. Not merged into the playable game's canonical kernel and not tested on native Windows. GitHub remains a repository/documentation destination, not the build host.

## Physical three-owner delivery

- **Owner 7** operates the source colony ship and legitimately constructed producing warehouse. v53 verified factory returns supply 1 kg aluminum frames and 1 kg aluminum panels, each with exact immutable factory-return witness.
- **Owner 8** operates an independent finite-capacity carrier colony ship. A distinct host-only `HostTransferConsignedStock` action moves goods from owner 7's store to owner 8's real ship cargo. Ordinary `TransferStorageStack` retains its original owner-only behavior.
- **Owner 9** independently mines, refines and fabricates, jumps to a second system, constructs an outpost and completes its own warehouse before taking delivery. Owner 8's carrier then jumps from system 1 to system 2 and physically accelerates/brakes to the receiving warehouse's docking range before unloading.
- The final inventory exists only in owner 9's actual constructed store. The carrier no longer owns the delivered stacks; source inventory is decremented; original network escrow remains 8 kg; total world material conserved.

## Durable host authorization and receipts

- Dedicated `ThirdPartyRegistry` stores immutable one-item-code, whole-kg, factory-backed booking with source/hauler/receiver IDs, independent owners, both built warehouses, destination system and uniquely reserved load/unload command IDs.
- Source owner must authenticate to submit a proposal. Separately authenticated roles 1/2/3 approve as source owner, carrier and receiver. The registry requires an injected host-authentication function and refuses wrong/duplicate approvals. **The focused fixture callback is not a genuine multiplayer session or cryptographic identity provider.**
- Atomic counts in `world_meta` anchor contract, approval and revocation records. The world admission path and the later SQLite writer transaction both enforce those grants and the reserved command identity.
- Revocation is committed before loading only: a fully approved revoked order cannot load; after physical loading, revocation is refused until a durable diversion/return workflow exists.
- Ledger replay verifies exact cargo/storage endpoints, real jump chronology, loading before delivery and no unmanifested carrier-stack traffic.

## Verification performed locally

| Test | Result |
|---|---|
| Strict GCC C++17 Release full CTest | **42/42 passed**, single final run |
| GCC Debug UndefinedBehaviorSanitizer focused v56 test | **1/1 passed** |
| Independent fresh ZIP extraction, C++17 rebuild and focused CTest | **1/1 passed** |
| Internal archive file SHA-256 verification | **327/327 passed** |
| v55→v56 patch `git apply --check` and applied-file comparison | **9/9 byte-identical** |

Negative cases executed include anonymous proposals, forged owner identities, role impersonation, incomplete or duplicate approval, unauthorized standard command routing, source double-spend, cross-system remote delivery, premature unloading, post-load revocation, interrupted load and unload at two SQLite failpoints, exact retry, duplicated delivery refusal, independent replay, missing contract/approval/cancellation, and forged ledger/destination receipts. These are local deterministic tests, not production identity or multi-process security acceptance.

**Deliverable:** `SingleGalaxy_DEMOG05q6e5b8f_IndependentCustody_v56.zip`, **1,027,019 bytes**, SHA-256 `8cc6a5444a21494ca7992d07dab94da67a3e8b21ad5a87023d54cccb34205d4f`. Includes full focused source, 42-test CMake configuration, new v56 test and implementation, nine-file v55→v56 patch, engineering report, release/UBSan/fresh-extraction logs, and per-file SHA256SUMS.

**Source base:** `SingleGalaxy_DEMOG05q6e5b8e_ShipmentManifests_v55.zip`, verified SHA-256 `8bdf266e7bc02f9070f3587c5363988d168fe4438e83325e945a8c8d7358c3bb`.

## Explicitly uncompleted

Actual multiplayer session authentication, key management or externally cryptographic receipts; general delegated warehouse account controls; post-load revocation/diversion/return and contract supersession; arbitrary material taxonomy, split lots, payment/market settlement, fully routed distributed cold/detailed region processes; real production machine/staff/utility binding; merge into the complete original game; and native Windows/game-client acceptance. Do not call these production-ready.

**Next recommended gate:** `DEMOG-05q6e5b8g` — host-authenticated consent/dispatch, cancellation diversion and two-party settlement, followed by split-lot/multi-region synchronization and full-game merge. Dependent fatalities remain blocked, and police/fire/institutional EMS deferred.
