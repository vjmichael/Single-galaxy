# COLONY-AUTO-04D.4D3A — Two households, one atomic colony rescue (v77)

**Engineering date:** 2026-11-03. **Parent:** v76 / PR #12.

## Implemented execution contract

The native v59 `HostColonyFamilyDeparture` world command already supports an ordered union of real adult and reserved-high-range physical dependent identities. This milestone extends the corresponding **host-only ART02/ECON03 execution adapter** to exactly **two disjoint two-parent households**, each with one or more ART02-born children. All members must share one already-registered family scope, origin colony, regional scope, and one nearby receiving vessel; exactly four living adults and at least two children are required. Maximum physical incoming headcount remains 16, with all existing critical-danger, proximity, owner and real food/water/oxygen conditions enforced.

A v77 `ExecutionPlan::households` explicitly partitions adult IDs into ordered guardian / co-parent pairs, with unique guardian and co-parent move event IDs, and sorted child ID / move event bindings. The first guardian move event is the authoritative world command. Every admitted child must already have one native `PhysicalDependent` with matching original parent pair, physical origin, complete prepared manifest, and ART02/canonical birth provenance. Different guardians cannot claim the same child or adult; every source adult and every child must be accounted for. `ExecutionPlan::households` must be empty for the previous two-adult format and legacy event fields must be empty for the two-household format.

One real world step physically relocates the entire manifest. Inside its **single `PersistentWorld::Store::commit` transaction**, `NpcAppearanceStore::record_guardian_household_moves` runs for both parent-pair partitions with incrementing ART02 family revision CAS, then **one** `CheckpointStore::commit_migration_in_world_transaction` moves the combined population and journals the single regional revision. Each adult and dependent move has an immutable individual receipt in `sg_colony_family_household_member`. Any invalid pair, missing child, stale family/region token, failed step or injected SQLite crash rejects the entire effect. The preexisting resource conservation and child-specific consumption behavior is retained.

## Replay, tamper and fixed provenance

Independent world reconstruction requires an immutable prepared intent, the **pinned pre-departure physical world snapshot** from v75, an applied world command, ART02 moves for *every* member and the matched ECON03 population receipt. For v77, reconstruction also proves the child's original parent pairs form exactly two disjoint pairs covering all four actual relocated adults, rather than trusting only population totals or receipt labels.

`tests/COLONY_AUTO04D4D3A_TwoHouseholdTests.cpp` creates two actual ART02+ECON03 births via the host bridge (not fake birth rows), verifies both families, attempts a missing child, duplicated event, duplicated child, reused guardian, reused co-parent, reused world event, mixed legacy/v77 manifest, and a simulated mid-transaction failure, then performs one physical 4-adult+2-child departure, validates source and receiver ART02/ECON03 counts, verifies eight real consumers on the receiving ship, restarts, independently recovers a copy, and refuses an edited history missing one child's move receipt.

The focused GitHub Actions native suite includes the new test and the seven predecessor regression tests.

## Not yet supported

Distinct **ART02 family scopes** at one colony, children entrusted to **nonparent legal guardians** outside their original parent pair, all-other-adult workforce arrangements, two receiving vessels, multi-hop transfers, and migration during warm/cold transition are **not** generally supported. v76 remains in force: ownership transitions refuse the rescue, including stale tokens after an aborted handoff. Further work must not claim those capabilities merely because the two-household population arithmetic works.

**Status:** draft integration branch; full v77 CI result required before acceptance.
