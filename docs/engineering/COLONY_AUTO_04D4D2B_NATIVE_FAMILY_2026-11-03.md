# COLONY-AUTO-04D.4D2B1 — Native physical family transfer (v75)

**November 3, 2026. Source branch:** `integration/colony-auto04d4d2b-native-family-v75-20261103`; parent v74 `e330b99b5`.

## Scope and protection boundaries

This is the native simulation prerequisite for the full cross-ledger family evacuation, **not** a certified colony evacuation. The old host-only `HostColonyDeparture` command now carries registered physical children alongside the entire adult manifest, atomically within one deterministic simulation tick. Every dependent must still be physically resident at the departure source and their registered guardian must be in the complete adult departure roster. Child occupancy counts against the sixteen-person bounded local rescue manifest and receiver's real food, water, and oxygen runway, without increasing worker count.

A v60 canonical snapshot records `relocated_dependents` beside `relocated_npcs` in each `ColonyDeparture` receipt. Reload and replay must verify that the dependent and guardian actually occupy the destination and no child is left on the evacuated hull. Earlier v58/v59 snapshots remain decode-compatible. In particular, submitting an adult-only departure no longer reduces the snapshot version from 59 to 58.

The normal `PersistentWorld::submit_host_colony_departure` and `step_with_colony_departure` **remain fail-closed** for any ART02 births or child travel links. The standalone native `Simulation` test exercises only physical movement and serialization; it does not bypass the separately authoritative ART02 and ECON03 registries. No persistence API is added that can advertise a completed family evacuation without both ledgers.

## Focused native acceptance

`tests/COLONY_AUTO04D4D2B_NativeFamilyTransferTests.cpp` verifies that the receiving ship owns all original adults and registered children in one native event, the child is still not a worker, no material is minted, safety-critical source preflight is required, incomplete or duplicate adult rosters are refused, v60 pending and applied snapshots round-trip, child intake changes actual 20-tick food consumption, and a forged mismatched guardian is rejected on snapshot restore.

Run `cmake --build build --target galaxy_colony_auto04d4d2b_native_family_tests` and `ctest --test-dir build --output-on-failure -R galaxy_colony_auto04d4d2b_native_family`, or inspect the extended `colony-auto04d4d2.yml` CI workflow.

## Next required gate — COLONY-AUTO-04D.4D2B2

Introduce a dedicated, host-only complete household commit. Under one writer-locked SQLite world transaction, verify the frozen prepared family manifest and v74 physical registration authority; execute and journal the native v60 departure; write ART02 `record_guardian_household_moves` and the matched ECON03 `commit_migration_in_world_transaction` population delta; persist a durable receipt tying child IDs, guardian IDs, command, next world generation, regional owner revision and destination; then extend startup, independent replay and `recover_copy` auditing. Any failed CAS, absent child, partial family, unrelated scope, cross-owner destination, inconsistent cold/warm ownership or injected crash must reject or roll back the entire departure. Only after this integration passes may the host classify a family evacuation as completed.

