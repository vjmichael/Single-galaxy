# COLONY-AUTO-04D.4D2 — Executable Atomic Whole-Family Departure (v74)

**Date:** November 3, 2026. **Base:** COLONY-AUTO-04D.4D1 v73. **Branch:** `integration/colony-auto04d4d2-physical-family-v74-20261103`.

## Physical identity model

A named ART02 child is **not a playable/native adult NPC and cannot be used as labor**. v59 opt-in new-world snapshots add `PhysicalDependent`: a stable high-range child ID, birth parents, immutable birth command, original home and current physical ship. It consumes **one actual unit of food, one of water, and one of oxygen per 20-tick needs cycle** from its real compartment. Crew demand + physically present dependent demand are calculated together in the native consumer and colony safety assessment. There is no extra resource source and no double-counting of an absent guardian or child.

`Simulation::enable_physical_dependent_accounting()` is genesis-only (requires prior v57). Existing v58 and earlier saves retain their original historical semantics. In v59, `step_with_atomic_dependent_birth` additionally executes `HostRegisterDependentBirth` on the real simulation, **in the same authoritative SQLite transaction** as the existing ART02 child birth and ECON03 cold-owner regional population increment. It records a distinct live physical child location and an applied world command receipt. The canonical `galaxy-kernel-v59` format stores each high-range physical child, lineage and immutable birth command and refuses duplicate/missing birth proof.

## Physical complete-family movement

`Action::HostColonyFamilyDeparture` is a host-only action with a canonical sorted union of living adult NPC IDs and registered high-range child IDs. A valid command moves the **entire** endangered source colony's people in one native step, including source operating crew and any supported farm/habitat adults, into one nearby stocked, same-owner receiving vessel. Adults become receiving crew; children remain child identities, change physical and permanent home to the receiver, and keep consuming actual receiving food/water/oxygen. The source ship remains an **intact but unoperable evacuated hull**, while empty buildings retain local supplies. The command has material transfer=0; it never simulates new cargo or moves a child by changing a census alone.

The v59 departure snapshot records **separate exact adult and dependent rosters** and validates each person's final physical location, identity, and applied world receipt on reload. An adult-only departure cannot operate on a source with real registered children.

## Cross-store atomic authority

The 04D.4D1 immutable `sg_colony_family_departure_prepare` record is still required. A new host-only `step_with_atomic_colony_family_departure(ExecutionPlan)` revalidates exact world generation/hash, source and receiver, all parents and high-range children, real receiving provisions, the family census, and the cold regional checkpoint. The initial executable acceptance is deliberately limited to a **complete two-parent household and all its children**, at most 16 people, on one receiving ship. Deceased/missing/transported dependents, unregistered birth bridges, an unaccounted second family, forged/out-of-date owner, cross-owner transfers, or a missing ART02/ECON03 revision cause hard refusal.

One exclusive `PersistentWorld::Store::commit` transaction applies the actual world movement, ART02 `record_guardian_household_moves` (each adult and child with its own immutable custody event), ECON03 `commit_migration_in_world_transaction` (subtract entire household at source, add to receiver), and `sg_colony_family_departure_execute` (immutable world generation, source, receiver, family/region and exact headcounts). An exception or injected crash before SQLite COMMIT must preserve all original custody; the prepared receipt is not a rescued outcome. There is one canonical writer, not multiple ownership tables.

## Evidence and remaining acceptance

- `tests/COLONY_AUTO04D4D2_PhysicalFamilyTests.cpp`: native birth, real child resource demand, no extra adult workforce, refusal to abandon child via v58 adult-only departure, full family physical relocation, post-rescue consumption and strict snapshot restoration.
- `tests/COLONY_AUTO04D4D2_AtomicFamilyTests.cpp`: genuine ART02+ECON03 birth, prepared family manifest, wrong/stale/duplicate ID refusal, failpoint rollback, one applied physical adult+child move, family census, regional population, local supply and world-mass conservation, restart and independently recovered original journal.
- `.github/workflows/colony-auto04d4d2.yml`: focused Linux native acceptance and predecessor regression suite.

**Limitations:** This does not yet implement multi-parent separated family custody, post-mortem authorized nonparent guardians, multiple receiving ships, distant multi-hop voyages, long-distance supply escrow or general warm/cold population handoffs. The narrow 16-person nearby rescue is implemented first, with all unsupported cases fail closed. Broad integration and independent Windows acceptance remain necessary.

**CI status:** Native results must be checked on the final branch head before declaring acceptance.
