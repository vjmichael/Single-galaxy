# DEMOG-05q6e5b8a — Two-machine production network and cold/detailed handoff (v51a)

**Date:** 2026-11-03. **Project:** original Single-Galaxy simulator (not EVEmu).
**Baseline:** focused v50a C++17/SQLite real one-line cold/detailed factory handoff.
**Status:** **Implemented and independently verified in a standalone focused source archive**. NOT merged into the full game production kernel, constructed factory system, desktop UI, or native Windows release. GitHub is repository and documentation, not the build host.

## Implemented and verified

1. `RegionalProductionNetwork` manages **2–8 ordered machine IDs**, each with distinct actual named NPC workers, their employment worksite, a validated mass-conserving `QuantifiedLine` recipe, and physical feed, finished, scrap and WIP stores. The region also owns a bounded shared warehouse.
2. Exact-gram `NetworkTransfer` moves material only between permitted source/destination stores. Source withdrawal and destination deposit, network payload and immutable effect receipt commit in **one SQLite transaction**; failed writes do not duplicate material. The destination machine must accept the actual material code. A total-mass invariant covers warehouse, all machine stores and every reserved WIP input.
3. A versioned `SG-NET-51` network checkpoint, durable immutable effect chain and independently decoded/replayed transfer instructions preserve the entire graph across restarts. Missing, forged or interrupted material receipts are rejected.
4. A unique host-attested initial stock receipt can seed the material warehouse only once. **The old v50 single-line bootstrap is also fenced from reusing that receipt in the same database.** This is an escrow receipt, *not yet* an authoritative physical debit from the main game's constructed warehouse/ship cargo.
5. Real cold scheduler `CheckpointStore::commit_command` advances production and life-support time with the network payload and journal in one transaction; another regional command cannot bypass enrolled industry.
6. `RegionalModeAuthority` can promote all machines, warehouse materials, WIP, and worker availability from Cold→Detailed and demote them together under owner generation and checkpoint revision fences. Empty staging is rejected; aborted detailed→cold transition rotates the active network generation. Mid-write injection rolls the entire stage back.
7. Production uses actual NPC IDs and the canonical persisted world location. If the smelter's named workers embark on a real ferry, that machine stops. The second staffed machine can continue using its already-stocked input; the smelter resumes after committed return.

## End-to-end scenario

Six actual colony residents across two nonoverlapping ART02 family scopes. A warehouse begins with 10,000 g of host-attested ore. Machine 10 smelts 1,000 g ore into 950 g aluminum and 50 g tailings; machine 20 assembles 950 g aluminum into 900 g plates and 50 g offcuts. Genuine debited machine/warehouse transfers provide input and store completed goods. Two workers leave on a real passenger ferry: the smelter pauses, but assembly continues. The cold-only control and the cold→detailed→cold path have identical inventory, per-machine batch counts, WIP and regional clock at the agreed tick.

Tested fault/refusal cases: source over-withdrawal, incompatible output routing, duplicate worker identity, unique source receipt reused by either v50 or v51, empty owner stage, altered owner generation, failed warehouse transfer, failed warm production receipt, interrupted promotion and demotion, abort-and-retry, bypassing regional cold commands, database restart and independent event-chain verification, missing transfer event after offline edit.

## Measured local verification

| Check | Result |
|---|---|
| Strict GCC C++17 Release full suite | **38/38 PASS** (37 inherited) |
| GCC Debug UndefinedBehaviorSanitizer full suite | **38/38 PASS** |
| Fresh extracted ZIP, independent Release compilation and full CTest | **38/38 PASS** |
| Final ZIP per-file SHA-256 manifest | **289/289 matched** |
| v50a→v51a patch dry-run, apply and changed-source byte comparison | **8/8 files identical** |
| Final archive vs independently rebuilt archive source byte comparison | **95/95 files identical** |

**Deliverable:** `SingleGalaxy_DEMOG05q6e5b8a_Production_Network_v51a.zip`, **884,253 bytes**, full focused CMake/C++17/SQLite source, v50a→v51a patch, 38 native regressions, engineering reports, Release, UBSan and clean build logs, and SHA-256SUMS.
**Archive SHA-256:** `ce2924a142e391d43db031f5f1b6d1a59aeea9c73ceda539a52d815b502f9662`.

## Unfinished integration — NOT full factory-network completion

- This is a **bounded one-region, one-warehouse production network**, not the entire physically constructed universe-wide factory graph. Factory/machine IDs and starting stock still need a host-owned canonical construction and warehouse authority binding.
- **No canonical game-world material debit** yet: material warehouse begins from a unique host-attested receipt. The production mainline must withdraw ore from real ship/cargo/constructed storage and credit network escrow in the same world transaction.
- Direct authenticated transfers have no delivery-time simulation, conveyor routing, haulage capacity, market inventory, full power-line/maintenance supplies, shift qualifications or dynamic job proficiency. Machine power/maintenance are existing machine flags and separate detailed integration gates.
- No full main-game merge, 3D client, Windows validation, cross-region distributed transaction or complete independent cold-vs-detailed network-wide replay under all game activities is claimed.
- Dependent fatalities remain disabled; **children must never be visually depicted harmed or dying**. Police/fire/institutional EMS remain deferred.

**Next:** DEMOG-05q6e5b8b — real constructed-world warehouse/cargo debit, machine construction binding, finite logistics shipments and utility/shift authority; then integrated full factory graph cold/detailed handoff and actual Windows/GUI acceptance.