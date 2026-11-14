# COLONY-AUTO-04D.4D2B2 — Atomic family evacuation across the world, ART02, and ECON03

**Date:** November 3, 2026 (Arizona). **Version:** v76. **Branch:** `integration/colony-auto04d4d2b2-atomic-family-v76-20261103`. Built on verified v75 native dependent movement and v74 immutable ART02 birth registration.

## The transaction boundary

Family evacuation comprises three distinct phases. The earlier *preparation* record proves that a complete host-authorized family and its real receiving supplies were checked; preparation does not move any person. *Submission* queues the existing `Action::HostColonyDeparture` native command and does not move anyone either. The **only execution** is `PersistentWorld::step_with_atomic_family_departure`. It prepares the next canonical world in memory, then invokes the existing `Store::commit` writer with a single SQLite `BEGIN IMMEDIATE` transaction. That transaction includes the applied world receipt and v60 snapshot, all ART02 individual residence moves and census revisions, one cold-owner-fenced ECON03 regional checkpoint population delta, the region's immutable command receipt, and new versioned family/member evidence. The in-memory world is swapped only **after** SQLite COMMIT succeeds.

The new `PersistentWorld::AtomicColonyFamilyPlan` identifies the original prepared intent and generation, complete adult/child manifest, immutable ART02 move-event IDs, source and destination colony IDs, family census CAS revisions, regional revision, and owner generation. A second read of the original preparation, physical dependent registration authority, and world/ART02 family audit occurs **inside** the execution's writer-locked transaction. Stale region owner state, family revisions, failed world movement, incomplete household, duplicate event, missing real birth or physical child, cross-owner destination, insufficient supplies, and any failed SQL insert all abort the transaction.

## Identity and conservation

The new `NpcAppearanceStore::record_complete_colony_moves` is a *borrowed-transaction* ART02 batch, not a shortcut that allows an adult parent to abandon a dependent. It requires a finite complete source census, sorted canonical members supplied by the world authorizer, independent birth closure for each child, distinct receipts, physical source→receiver custody evidence, and incrementing source/destination census CAS per member. It leaves original parentage and birth registration unchanged. The worker/NPC count stays constant: ART02-born children remain physical dependents, not worker clones.

`RegionalCheckpointStore::commit_migration_in_world_transaction` keeps its original `society-migration-host` behavior for ordinary migrations and accepts a separately whitelisted `colony-family-evacuation-host` receipt actor for this same cold-owner population CAS. Source and destination totals move by the identical finite member count. No arbitrary people, food, water, or oxygen are minted. The receiver's actual supplies and total berth capacity include physically present children.

## Recovery and independent reconstruction

`sg_colony_family_evacuation` is an immutable per-command header and `sg_colony_family_evacuation_member` contains every adult/child event, kind, guardian and source/destination census revisions. Creation and immutable triggers are inside the executing world transaction; both are independently checked at startup.

The restore verifier checks the applied host command against the v60 world departure, original prepared manifest and original world hash, original immutable ART02 child-registration source and guardian, every ART02 move, complete adult/child roster, contiguous census revisions, and separate regional receipt. It reloads pinned snapshots from **both before and after** the actual evacuation and verifies identical physical identities with no child or adult left at the source. The canonical recovery copier performs these checks and separately rechecks immutable physical birth authority.

The original registered home of each child remains immutable in `sg_colony_physical_child_authority`. Once evacuated, the child's current home differs only when the full authoritative evacuation/member receipt independently proves that change. The abandoned source hull remains an inert physical asset; its local consumers become zero.

## Acceptance

`tests/COLONY_AUTO04D4D2B2_AtomicFamilyTests.cpp` creates a real ART02 high-ID child by `step_with_atomic_dependent_birth`, physically registers it under guardian 1, and evacuates both parents and the child using an already provisioned ship. It checks:

- incomplete child manifests refused, prepared manifest required, duplicate execution impossible;
- generic `step()` bypass blocked;
- deliberately stale regional revision rejected *after* the in-memory movement without committing any world, family or regional change;
- injected `FailPoint::AfterSnapshotWrite` rolled back entirely, with the original parents, child, population and supplies still authoritative;
- successful +3/-3 regional population conservation, all three ART02 moves, physical child occupancy and guardian continuity;
- real food consumption at the receiver is five people (two resident adults + two relocated adults + the child), with no extra worker or material;
- normal restart and independent `recover_copy` validate the completed immutable family custody chain;
- a privileged deletion of a single child member row plus its delete trigger causes fail-closed restart.

**Linux CI:** [run 41770302556](https://github.com/vjmichael/Single-galaxy/actions/runs/41770302556), **five focused CTests passed**. Tests build against actual SQLite on the v76 branch, not synthetic in-memory mock databases.

## Boundaries and next extensions

This gate is for a bounded, complete, locally reachable family cohort whose recipient is a real stocked ship under the same player owner and whose regional authority is a verified *cold* owner. It deliberately refuses cross-owner, distant/inter-system, incomplete-census, unauthorized warm-owner and unsupported rescue routes. No transfer is authorized just because the player requests it. A prepared manifest is never an evacuation receipt.

For expanded acceptance, test multiple children with different guardians, valid nonparent legal custodians, staffed satellite sites, whole-region hot/cold transition interference, and additional tamper cases. These are separate quality gates; they are not prerequisites for the verified narrow v76 transaction.
