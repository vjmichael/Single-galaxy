# DEMOG-05q6e5b8b — Authoritative constructed-world warehouse debit (v52)

**Date:** 2026-11-03  
**Project:** Original Single-Galaxy C++17 / SQLite simulator, separate from EVEmu.  
**Baseline:** Locally verified DEMOG-05q6e5b8a v51a two-machine production network.  
**Status:** **Implemented and independently tested as a focused local source package. Not merged into the complete game kernel, client or native Windows release.** GitHub is repository/documentation only.

## Implemented physical stock custody

The host builds a real outpost and storage by normal material-consuming game actions, mines and refines geological aluminum, then deposits **10 kg** into that actual constructed store. A new host-only `HostEscrowConstructedStock` world command transfers the existing physical stock into a network escrow. The source store's available goods decrease by **10 kg** while its canonical `network_escrow_kg` increases by **10 kg**; concurrently, the `RegionalProductionNetwork` production warehouse receives **10,000 g**. This is **one physical mass represented across two reconciled ledgers**, not additive stocks.

The world command submission and execution, world snapshot, historical event, original outpost source/destination ledger, immutable `sg_v52_world_escrow` receipt, production network seed checkpoint and its immutable effect receipt all commit in **one SQLite BEGIN IMMEDIATE** transaction. A failure after the network payload, after the effect record, or after the physical world snapshot rolls back every part. In-memory publication only happens following COMMIT.

Existing snapshot formats remain readable. New `galaxy-kernel-v52` stores the outpost's reserved stock separately from available physical goods; legacy action ordinals and SQLite `operation_log` schema remain unchanged. The new privileged command deliberately represents atomic submit+execute in one `kind=1` operation. Historical generation validation, replay and journal recovery recognize that action specifically and advance its world tick exactly once. The independent resource ledger checks exact applied command, source constructed site, escrow destination, quantity and tick.

Initial stock is restricted to real **whole-kg aluminum**, an owned constructed outpost storage site, one authoritative source ship and one bounded network scope (two to eight machines, one network warehouse). No source receipt can bootstrap a second network or the older v50 single-line factory. The existing deterministic network manufacturing and material conservation remain intact.

## Verified local evidence

| Check | Result |
|---|---|
| GCC strict C++17 Release CTest | **39/39 passed** (38 inherited + 1 new) |
| GCC Debug UndefinedBehaviorSanitizer full CTest | **39/39 passed** |
| Final ZIP clean extraction / independent Release build / full CTest | **39/39 passed** |
| Final ZIP SHA-256 source manifest | **289/289 matched** |
| Extracted source vs independently tested source byte comparison | **96/96 identical** |
| Pristine v51a→v52 patch dry-run and application | **8/8 changed files identical** |
| Fault injection, safe retry, restart, independent genesis journal recovery, corrupted source/destination ledger | **Passed / rejected correctly** |

New target: `demog05q6e5b8b_world_warehouse`. It exercises actual colony construction, mining/refining, physical aluminum stocking, three separate injected rollbacks, exact debit/credit, world mass conservation, duplicate-source refusal, restart, independent recover-copy, two-machine feed/production, deleting escrow provenance and an offline-forged world resource-ledger destination.

**Local source archive:** `SingleGalaxy_DEMOG05q6e5b8b_Constructed_Warehouse_v52.zip` (**905,326 bytes**); **SHA-256** `0e6b6d5f20e78b20208243207b8e174da835c05a35636228ca5847c89675a7bf`. Includes focused native source, v51a→v52 patch, 39 regression tests, engineering report, Release/UBSan and clean-extraction build/test logs, SHA256SUMS.

## Strict remaining integration boundaries

- **This is a one-direction constructed-storage to production escrow bridge.** Finished manufactured goods are not yet credited back to constructed world/ship warehouse stock. That requires a matching authoritative release/credit and physical material-code reconciliation; do not count the two ledgers twice.
- One constructed outpost, one aluminum material code, whole-kg physical source amount and a small bounded manufacturing network; not a complete arbitrary item/cargo/warehouse or player-built factory graph.
- No full construction machinery IDs, actual transport delay and conveyor logistics, utility service/maintenance, labor qualifications and shifting, local multi-store transactional haulage, main-game source merge, native Windows or client validation.
- The on-disk receipt consistency and SQLite trigger checks are not externally cryptographically authenticated against a malicious editor rewriting every related record.
- Dependent fatalities remain disabled; **children must never be visually depicted harmed or dying**. Full police, fire and institutional EMS remain deferred by user direction.

**Next:** DEMOG-05q6e5b8c — atomic finished-goods return from factory escrow to canonical constructed warehouse, multi-material stock and two-way custody replay; then real machine construction/utility/logistics integration.