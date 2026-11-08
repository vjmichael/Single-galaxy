# SPACE-05K — Cross-system physical interaction integrity audit
**Date:** 2026-11-08. **PR:** #95, branch `space/worker-io-deadline-20261107`. **Classification:** real historical local testing PASS; modern v120+ source repair and tests implemented; current-head full-kernel native acceptance **OPEN**.

## What was actually tested locally

The complete original Single-Galaxy **v66** source archive was recovered from the owner's Library `SingleGalaxy_COLONY_AUTO04B_NativeHostSafety_v66_2026-11-03.zip`, byte-verified SHA-256 `54f041f93e1a2ac7bab8984f82fa90c95dc80aa4cd33b8a6e10553a2d6c5b454`, unpacked and compiled via **GCC 14.2 + CMake 3.31.6 + Ninja + system SQLite**. This is the actual historical original game kernel, not a fabricated physics, passenger or cargo mock.

Two local CTest groups compiled and **passed 22/22**:
- First **9/9**: `galaxy_b62a_gate_transit_tests`, `v59_atomic_split_rescue`, `v59_crash_custody`, `v59_crew_split_rescue`, `v59_dependent_rescue`, `v59_dependent_rescue_outcome`, `v59_multi_family_rescue`, `v59_strand_rescue`, `v59_third_party_custody`.
- Additional **13/13**: `v59_cross_system`, `v59_dependent_custody`, `v59_dependent_incident_accounting`, `v59_dependent_incident_journal`, `v59_ferry_loss`, `v59_ferry_loss_integrity`, `v59_mixed_ems`, `v59_mixed_integrity`, `v59_physical_handoff`, `v59_society_atomic`, `v59_society_reconciliation`, `v59_world_finished_goods_logistics`, `v59_world_finished_goods_return`.
- Two actual executed JUnit files and build/CTest logs exist in the local evidence ZIP. These results **do not certify** the newer PR-head v120-v121 native kernel.

### Defect reproduced in the actual historical game

Compiled a standalone real `Simulation` integration probe linked to the full v66 `galaxy_kernel.a`: one ordinary player colony ship and one uncrewed ship **250,000 mm apart** in the same system; submit an authorized `Action::HostDestroyShip` command. Actual local output:

```text
v66 HOST DESTROY remote 250000 mm result=0 victim_is_wreck=1 ore_conserved=1
```

This shows historical server-hosted destruction **accepted a remote target with no local physical encounter**. `0` is `Outcome::Applied` in the tested v66 code. This is a reproducible old-kernel contract problem, **not proof** that the v120+ repair has executed.

## PR #95 fixes

1. `src/Simulation.cpp` `Action::HostDestroyShip`: for **snapshot v120+**:
   - No longer reject purely because attacker and target have different administrative star-catalogue IDs.
   - Require `same_local_frame` and `within_native_3d_range(attacker,victim,kTrainingWeaponRangeMm)` (current radius **12,000 mm**), refusing remote or non-authoritative targets with `Outcome::OutOfRange`.
   - Keep **pre-v120 legacy save and incident behavior unchanged**, avoiding an unreviewed migration.
   - Preserve existing trusted host authorization, target validity, no attack on colony ships, source wreck identity and cargo conservation.
2. `tests/NativeGalacticProbeSupervisorTests.cpp` adds `cross_system_ship_destruction_custody`: create two original uncrewed training drones with recoverable ore; execute a **real gate transit** so the attacking colony ship is filed in system 2 while the nearby drone remains in system 1; assert authoritative physical proximity; require far 250 m ship refusal and near ship destruction; verify no new ore/NPC/entities, identical wreck ID/owner/cargo, non-repeatable destruction, and canonical snapshot recovery.
3. `tests/FLIGHT05_PhysicalRescueTests.cpp`: preserve the complete set of named NPC IDs through a trusted passenger ferry rescue and SQLite reopen; totals alone previously could not detect identity substitution.
4. `scripts/space05k_local_crosssystem_integrity.sh`: strict reproducible exact-HEAD **local Linux** acceptance: checks the supplied Git SHA and clean tracked tree, builds the real game kernel and 13 targeted executable tests, refuses missing CTests, emits compiler provenance, build logs, JUnit and status. The 13 include new galactic supervisor/wreck regression, FLIGHT05, FLIGHT06, PLAY09 and historical cross-system/ferry/custody tests. No Docker, no synthetic substitute.

## Test status — do not confuse code coverage with execution

| Gate | Result |
|---|---|
| Historical original-game v66 local compilation | **PASS** |
| Historical v66 22/22 custody/gate/rescue CTests | **PASS** |
| Historical remote-destruction flaw on real kernel | **REPRODUCED** |
| Modern v120+ no administrative wall + physical-range repair | **SOURCE COMMITTED**, not current-head executed |
| Modern gate-crossed near/far destruction and snapshot regression | **SOURCE COMMITTED**, not current-head executed |
| Modern exact-NPC identity ferry rescue checks | **SOURCE COMMITTED**, not current-head executed |
| Exact PR-head full original kernel native CMake/CTest | **OPEN** (complete source not available to local container because outbound Git clone/codeload unavailable) |
| Windows native Job Object and second-chance MDMP | **OPEN** |
| Installed multiplayer four-cell sustained ship/passenger/cargo frame latency | **OPEN** |

GitHub was available through the **connected GitHub connector**, but the container could not resolve `github.com` or retrieve a complete current PR-head archive for a local checkout. A previous historical v66 source archive was in the owner's Library and used for the fully executed local tests. Do **not** pass v66 tests off as PR #95 acceptance. The exact-head local script is provided to close this once a real checked-out repo is available. Prior GitHub Actions often failed with zero executed steps; inspect steps/logs before classifying.

## Further cross-system physical custody gates

- Ship-to-ship ferry rescue of complete manifest where rescue ship crosses a genuine galactic catalogue boundary, including failpoint rollback and exact NPC IDs upon SQLite reopen.
- Mixed ship destruction and dependent rescue with original passenger/guardian/medical provenance and no duplicated/lost people.
- End-to-end physical freight possession and wreck salvage across a real sector boundary, including rejected remote actor, forged custody and crash/restart.
- Real native Windows/Linux results at the same final SHA, then 4-cell 1,024-tick original world and installed frame-time acceptance.
- Cross-system ship combat/host destruction should not be confused with arbitrary server emergency events. If remote incident authority is ever needed, implement a separately named and audited mechanism rather than quietly granting infinite host weapon range.

**Safety/data constraint:** Do not publish user-owned MSVC/Windows SDK archives to the public repo; only source/tests/logs and private owner Library evidence.
