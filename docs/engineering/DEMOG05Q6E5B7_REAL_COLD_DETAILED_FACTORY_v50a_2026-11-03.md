# DEMOG-05q6e5b7 — Real cold/detailed factory and workforce handoff (v50a)

**Date:** 2026-11-03. **Project:** original Single-Galaxy C++17/SQLite kernel, separate from EVEmu.
**Baseline:** verified local v49 focused source and DEMOG05Q6E5B7_REAL_WARM_INDUSTRY_HANDOFF_ACCEPTANCE_2026-11-02.md.
**Status:** **Implemented and tested locally for ONE material-backed quantified production line per regional owner.** This is not a main-game merge, network-scale factory implementation, native Windows validation, or playable UI. GitHub is source/documentation repository, not development host.

## Actual machinery, resource and workforce state handoff

- New `RegionalFactoryHandoff` persists complete `QuantifiedLine` feedstock/finished/scrap stores, actual reserved WIP mass and cycle remainder, machine tier, operation, power/maintenance flags, named worker IDs, tick, and completed batches in one SQLite physical industry checkpoint.
- Initial ore stock is host-attested by a uniquely consumable source receipt: one receipt cannot seed two factory scopes. **The focused kernel does not yet independently debit ore from the main game's mining or warehouse stock; this is a future integration gate.** After that provision, every machine transformation preserves total grams in feed/WIP/finished/scrap.
- Cold production advances real `RegionalEventScheduler` life support and a matching industrial checkpoint in one `CheckpointStore::commit_command` transaction. Only the factory host's coordinated, receipted mutation is permitted once an industrial scope is enrolled.
- Real Cold→Detailed promotion stages exactly the same material/WIP state under `RegionalModeAuthority::prepare/commit` and a new `sg_warm_active` owner generation. The owner flip cannot commit without the matching factory stage and immutable effect record. Failed commit leaves the region in a recoverable prepared state.
- Real Detailed ticks advance that persisted industry state with the same deterministic quantified production engine, but worker availability is derived from canonical physical NPC location, home worksite and voyage custody. No work is performed when the named workers are physically traveling aboard a ferry; they resume after documented disembarkation.
- Detailed→Cold demotion writes all real material/WIP and elapsed detailed ticks into the cold checkpoint, advances the regional needs scheduler through missed cooldowns and waste flows, retires the active warm record and flips owner in **one SQLite transaction**.
- Owner and factory generation rotate together on aborted demotion. Ordinary cold commands, direct society population events, or empty handoff callbacks cannot silently run behind a registered factory. The durable effect chain is replayed through `QuantifiedLine` and reconciled to the active stored checkpoint on reopening.

## Concrete acceptance

A genuine six-person settlement contains two disjoint ART02 family scopes; two real NPC workers are assigned to an ore smelter. Each staffed five-tick cycle transforms 1,000 g ore into 950 g aluminum and 50 g tailings. The test starts with 10,000 g host-attested feedstock, moves a partially completed batch across Cold→Detailed, completes batches during detailed ticks, demotes, and verifies the same WIP and goods as an independently running always-cold control region at tick 12.

The test injects SQL failures after cold factory writes and after effect records; after staged promotion; after detailed writes; and during demotion. Reopen/retry does not duplicate inventory, production or owner generations. Invalid worker IDs, reused source receipts, uncoordinated regional cold commands, and no-op handoffs are refused. A direct world ferry command boards both workers: factory output stops in detailed mode and resumes after their committed return. The same absence and return are tested under cold ownership. Registered resident population remains six throughout; duty-available population changes appropriately. At tick 20, the actual regional cohort consumes six kg each of food, water and oxygen, with corresponding waste; warm and cold processing reach the same resource totals at tick 35. The final run completes five authentic batches and survives database restart. Forged factory inventory payloads are refused.

## Actual local verification

| Check | Result |
|---|---|
| Strict GCC 14 C++17 Release full CTest | **37/37 passed** |
| GCC Debug UndefinedBehaviorSanitizer full CTest | **37/37 passed** |
| Fresh ZIP extraction, independent CMake/Release build and full CTest | **37/37 passed** |
| v49→v50a patch dry-run, apply and six-file byte comparison | **Passed** |
| Final ZIP internal per-file SHA-256 manifest | **277/277 matched** |

**Deliverable:** `SingleGalaxy_DEMOG05q6e5b7_Real_Industry_v50a.zip`. Full focused source, six-file v49→v50a patch, 37 native regressions, Release/UBSan/independent clean-build logs, engineering report and 277-file manifest.
**ZIP SHA-256:** `210a95c699c6a6242f387008c5a98f310d09b360a50d55e2f0cb61b1c54cffd7`.

## Remaining scope: do not call full industry integration complete

- The module covers one quantified production line in one authoritative region. The complete player-built factory graph, connected machines, conveyors, source inventory/warehouses, utilities, logistics and durable scheduled job assignments are not integrated.
- Named workforce proof validates real world location, NPC ID and employment assignment, not full injury/medical qualifications, job proficiency, shift rotation or cross-region employer authority. Unbound rescued ferry crew retain the conservative v49 handoff fence.
- One SQLite database, no multi-server distributed commit, full-game main-source merge, player client or native Windows acceptance.
- Dependent fatalities remain disabled and **children must never be visually depicted being harmed or dying**. Police/fire/institutional EMS remain deferred.

**Next:** extend the same checkpoint/effect framework to multiple machines and verified physical warehouse transfers, bind utility and shift supplies, establish regional rescued-crew worksite authority, then run whole-game cold/detailed promotion/demotion plus Windows acceptance.