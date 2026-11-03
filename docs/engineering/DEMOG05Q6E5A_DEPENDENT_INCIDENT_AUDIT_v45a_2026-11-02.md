# DEMOG-05q6e5a — Verified Dependent Incident Custody Audit (v45a)

**Date:** 2026-11-02
**Project:** Original Single-Galaxy C++17/SQLite simulator (not EVEmu server)
**Baseline:** Locally verified DEMOG-05q6e4 v44 focused source
**Status:** Implemented and tested in a local focused source archive. **NOT merged into GitHub main's full playable kernel, client, or Windows build.** GitHub is the repo and engineering history, not the development hub.

## New read-only audited custody model

- Adds `PersistentWorld::audit_dependent_incident(family_scope, original_transport, expected_children)` with a canonical, sorted, bounded, explicit caller manifest. It reconstructs each ART02 child's actual ferry and guardian chain from verified world and child-travel receipts, without a new population authority.
- Classification is `Aboard`, `Stranded`, `RescuedAboard`, `RescuedStranded`, or `Arrived`. The rescue history survives subsequent changes in ship mobility; a rescued child on a stranded rescue ship is not reported as simply traveling normally.
- Follows child ferry legs and any rescue successor by exact original/rescue transport IDs, evacuation command, committed generation, same guardian/co-guardian identities, and world physical voyage status. Conflicting active berths, missing/extraneous children, duplicate manifests, unlinked or forked rescue legs, missing guardians, and forged arrival states are rejected.
- This is a read-only custody audit. It does not mutate the world, advance logical time, create a child worker, conclude that a missing row means death, or claim a child casualty. Other completed past journeys remain valid without duplicating current custody.
- Fatal and mixed child-bearing ferry incidents **remain blocked** by the v44 host. No child harm or death is rendered or depicted. This follows the policy in `docs/engineering/DEPENDENT_INCIDENT_NON_GRAPHIC_POLICY_2026-11-02.md`.

## Local verification

| Check | Verified result |
|---|---|
| Strict C++17 GCC Release, full suite | 26/26 CTest PASS (25 inherited + 1 new) |
| GCC Debug, UndefinedBehaviorSanitizer, full suite | 26/26 CTest PASS |
| Final distributable ZIP: independently extracted, configured, rebuilt, full suite | 26/26 PASS |
| Final ZIP SHA-256 per-file manifest | 172/172 matched |
| Unified source patch against pristine v44 | `patch -p1 --dry-run` PASS |
| Archive integrity / final ZIP checksum | PASS |

New `demog05q6e5a_incident_accounting` covers parent-linked boarding, explicit incomplete and duplicated manifest refusal, physical ferry stranding/resumption, SQLite restart, rescue to a second ferry, second ferry stranding/resumption, arrival, independent genesis replay and negative corruption cases. All existing child rescue/casualty refusal tests remain passing.

## Source package and immutable exact identity

`SingleGalaxy_DEMOG05q6e5a_Dependent_Incident_Audit_v45a.zip`, **528629 bytes**, SHA-256 `d69ab2075115746157673fb40a7ad0777b46035327c59948f2b14bf26803064b`.
Includes complete focused CMake source, version v44-to-v45a source patch, 26 regressions, engineering notes, Release/UBSan logs, clean extracted build logs and 172 SHA-256 file entries.

## Not completed

- **No internally confirmed dependent death or missing-person transition yet.** Unaccounted evidence is a fail-closed error, NOT an in-world missing-person case. Fatal child outcomes remain prohibited until complete explicit adult+dependent survivor/deceased partition, guardian/orphan custody, immutable ART02 death/kinship events, ECON03 source/destination population settlement and one SQLite effect journal transaction are implemented and verified.
- No images, animations or cinematic depictions of children being harmed or dying. No new client visual presentation work.
- Cold/detailed ownership remains a conservative global active-dependent fence; region-scoped detailed workforce, production checkpoint reconciliation, and actual warm promotion/demotion remain unfinished.
- No full-game main source merge, native Windows acceptance, or police/fire/EMS institution integration; institutional services remain deferred as requested.

**Next:** DEMOG-05q6e5b — explicit fully manifested non-graphic dependent incident preflight and host outcome journaling, with survivor/missing/deceased semantics only as independently justified. Keep fatal incident commands disabled until immutable family/custody/population recovery is proven.