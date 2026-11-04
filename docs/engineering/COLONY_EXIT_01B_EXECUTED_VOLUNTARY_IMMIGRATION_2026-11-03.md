# COLONY-EXIT-01B — Executed single-adult immigration through the real DEMOG-05q6 machinery

**Date:** November 3, 2026. **Parent:** EXIT-01A PR #27 (`COLONY-EXIT-01`); unrelated political-petition PR #26 is closed and absent. **Status:** Draft pending focused/full CI.

## Game design rule

NPCs do **not** vote or petition to change government. When a real named colonist is exposed to severe shortages, they can become eligible to seek another home. A departure only counts if the resident survives a real voyage and is accepted by an actual receiving colony, with their identity, household registry and origin/destination regional populations consistently updated. There is no instantaneous teleport, headcount decrement, made-up legal guardian or free replacement worker.

## Existing immigration engine reused

The core protocol already exists, is not forked, and remains the authoritative migration mechanism:

1. `HostEmbarkPassengers`: host-authorized real boarding of an existing ferry with a specific `transport_id` and stable NPC identity. If passenger life support is enabled, real carrier stores must provide a conservative two-cycle buffer.
2. Real ferry travel: a physically executed gate jump, transit and arrival; an active voyage cannot be relabeled as completed without actual movement.
3. `HostDisembarkPassengers`: real passenger exit at the actual receiving ship. Their **physical location** becomes the destination while their **legal home** is still the origin.
4. `HostSettleMigrant` accepted command, followed by `PersistentWorld::step_with_atomic_migration`: the named NPC's home and crew assignments move in the same transactional world commit as `NpcAppearanceStore` ART02 family residence/census and ECON03 source/destination population delta. The original method already enforces real completed voyage, regional generation/owner and family-revision CAS, and refuses normal `step()` bypasses.
5. Restart and `recover_copy` independently verify migration provenance and deny forged family/region effect records. This integration adds its own admission/settlement link into that same atomic transaction.

## New EXIT-01B protocol

- `record_voluntary_immigration_admission`: requires the exact EXIT-01A immutable source shortage witness for the named NPC and unchanged governing epoch. Source risk must **still** be Critical or Emergency; destination must have healthy life support and capacity; actual source, receiving and ferry ships must exist; carrier must belong to source or receiver; a receiving-owner ID must be present and match the destination's actual owner. This method is a **trusted-host** operation: the host must authenticate the receiving owner's assent separately; matching a raw integer ID alone is not a cryptographic signature.
- The initial route supports **one physically resident adult, excluding the incumbent, unassigned specialists and dependent-parent obligations**. Any historical recorded child link at origin blocks this first narrow route rather than risking an orphan. A future household extension can reuse the already-existing paired-parent, guardian and child physical travel/migration mechanisms.
- Each admission is immutable, generation-CAS fenced and unique per original pressure witness and physical `transport_id`; stores source/destination owners, world hash/tick/generation, original government epoch, adult ID and ferry. Snapshot pruning retains historical admission worlds. It does **not** enqueue or complete movement.
- `submit_voluntary_immigration_boarding` accepts only a host boarding command whose ferry, origin, transport ID, **single real adult ID**, authority and current tick exactly match the admission; source distress and destination health must still be genuine. It delegates to the original physical passenger command admission and real `PersistentWorld::step()` for executed boarding.
- After the physical voyage and arrival, the caller submits the **same original HostSettleMigrant action** and calls `step_with_voluntary_immigration(admission, SocietyMigrationPlan)`. This wrapper links the original immigration plan to the dated admission. It requires exact NPC/source/receiver/transport IDs and a completed physical voyage; a pending or already settled permit is rejected. If this permit exists, the legacy `step_with_atomic_migration(plan)` cannot quietly drop the provenance link.
- The unchanged ART02 and ECON03 transaction writes an additional immutable `sg_colony_voluntary_immigration_settled` receipt **in the same SQLite BEGIN IMMEDIATE/COMMIT**. A rollback fault removes both the native residence/region population effects and this link. The `verified_voluntary_immigration(admission)` query is false until that commit succeeds.
- Normal startup and `recover_copy` replay each admission from its original pinned physical snapshot and verify the receiving owner, old local government, pressure and destination safety. Each completed admission must match exactly one native `sg_society_migration` receipt's NPC, transport, origin, destination, command ID and world generation. Missing or forged link fails closed; missing/forged parent admission also fails.

## Executable acceptance fixture

`tests/COLONY_EXIT01B_PhysicalVoluntaryImmigrationTests.cpp` builds a real poor colony (owner 7, six named crew), well-supplied carrier (owner 7) and physically separate receiving colony (owner 10, six named crew). The receiving owner moves its colony through a real gate. After the source depletes, NPC #2 obtains an EXIT01A pressure witness and the destination grants an admission. Invalid owner, wrong source NPC and replayed pressure fail. The adult boards their original ID, the carrier travels through the gate, and the individual disembarks. Before settlement their physical location and legal home disagree **intentionally**, and ordinary migration or time-step bypasses are rejected.

The same pre-existing SOCIETY migration plan then commits after a forced-failure rollback test. Expected: origin crew **6→5**, destination crew **6→7**, ART02 origin/destination census **6/6→5/7**, ECON03 regional population **6/6→5/7**, total still **12**, zero synthetic NPCs/casualties, and original government remains historically intact. Independent restart, recover_copy, admission-owner forgery and completed-receipt forgery must all be refused.

## Boundaries and follow-up

This gate demonstrates an **actual executed real adult relocation** with immigration receipt, not yet an entire autonomous personality/morale system. It does not assert a fully modeled personal decision, a scripted player immigration-policy pane (Open/Closed, talent filters, work permits), free passage to unauthorized foreign territory, cross-system route-finding, family move, voting, strike or military attack. Currently, the trusted host admits an individually pressure-qualified volunteer candidate and the receiving owner accepts; a later naturalistic AI scheduler should make the personal decision, consider route risks, arrange the ferry and provide a user-facing immigration policy. True household departures should reuse `step_with_dependent_transport` and `step_with_atomic_migration` with fully specified ART02 child/guardian records, not weaken this solo-adult path.

Linux CI focused workflow `.github/workflows/colony-exit01b.yml` tests this new execution together with historical grievance/diplomacy/governance and native v59 adult/household migration. Hardware Windows acceptance is still a separate gate.

## Verified native Linux acceptance

**Focused CI passed 7/7** at tested executable revision `be9b3a760e7560fbed291bd54cb9d86f8da4aa7c` ([Actions run 41787357199](https://github.com/vjmichael/Single-galaxy/actions/runs/41787357199)), 3.41 seconds CTest runtime. The seven native tests cover existing adult atomic migration, existing guardian/household migration, previous real incident/grievance/dated governance and EXIT-01A risk witnesses, plus EXIT-01B executed cross-owner immigration.

Hardening: `Simulation::completed_voyage_matches` exposes a read-only view of the canonical voyage's **exact ferry, origin, destination, status and full passenger manifest**. Both live settlement admission and independent original committed-world replay require this full manifest. An adversarial fixture rewrites the admission to refer to another genuine, fully stocked, physically nearby alternate ferry; the existing native voyage evidence rejects it despite plausible carrier ownership and matching transport ID. The final settlement also refuses if destination life support has become unsafe since boarding.

The test verifies rejected receiver-owner mismatch, wrong NPC, duplicate pressure admission, wrong boarding manifest, nonexistent admission, prematurity before physical arrival, legacy settlement bypass, failed atomic settlement rollback, retained origin legal home until actual settlement, source/destination crew 6/6→5/7, matching ART02 census and ECON03 regional population counts, stable named NPC ID, restart, `recover_copy`, and rejection of tampered owner, settlement command and substitute ferry. These are genuine executed kernel operations, not synthetic population ledger edits.

**Limit:** This is an explicitly host-scheduled survival-qualified single adult. An independent NPC-discontent scheduler and user-facing Open/Closed immigration policies are not yet implemented; the receiving owner's identity/approval must be authenticated by the trusted host. Complex household family moves must use the existing ART02 physical guardian/census route, not this limited adult-only adapter. No real Windows test is claimed.

**Complete associated GitHub CI matrix at executable SHA `be9b3a760e7560fbed291bd54cb9d86f8da4aa7c`: 18/18 workflows completed successfully**, including the family evacuation, child custody, emergency supervisor, previous incident/diplomatic history, prior individual-exit pressure, original v59 immigration and new real adult voluntary immigration. No failed or pending workflows remained when recorded. A documentation-only follow-up does not change compiled C++ or imply native Windows physical-hardware acceptance.
