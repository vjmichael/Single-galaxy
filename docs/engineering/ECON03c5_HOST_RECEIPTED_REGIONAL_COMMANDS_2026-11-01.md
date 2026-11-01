# ECON-03c5 — Host-receipted regional commands (2026-11-01)

**Status:** Locally implemented and tested as an opt-in regional module. **NOT merged into GitHub `main` or `PersistentWorld`.** This archive entry records engineering provenance; local artifact packages contain the source patch and verification files.

## Scope
- `RegionalHostCommands` provides a host-policy gate for region operations. The production host must authenticate the caller and check physical NPC work, carrier, cargo, route and demographic receipts. The current test policy is a stub, not a deployed authorization system.
- `CheckpointStore::commit_command` atomically writes the mutated scheduler checkpoint **and** immutable command receipt using one SQLite `BEGIN IMMEDIATE` transaction.
- Retries are keyed by (region scope, command ID), must match the authenticated actor and canonical request, and return the original outcome without reapplying inventory moves or notice acknowledgement.
- Old direct checkpoint saving is fenced once a region has received its first receipted command.
- Notice acknowledgement requires the expected outbox sequence, not a blind count. A real consumer must durably record delivery and dedupe by (scope, sequence).

## Local verification
A recovered isolated C++17/SQLite module build passed 4/4 CTest regressions covering ECON-03c2, c3, c4, c5. The new test covers policy denial, material conservation, restart-safe dedupe, ID collisions, stale-write rejection, rollback, exact-sequence notice ack, and region isolation. The complete ECON-03c4 source archive, full CTest/93-case JS suite, Windows GUI, and live-world integration **were not verified in this recovered workspace**.

## Local deliverables
- `SingleGalaxy_ECON03c5_Regional_Host_Command_Isolated_Source_Tests_2026-11-01.zip` — SHA-256 `fbe2665eac712024cccacad7a8e6d9dd4af2bae2583a337e1cd5a0068edcd925`
- `SingleGalaxy_ECON03c5_Host_Receipted_Regional_Commands_2026-11-01_Changes.diff` — SHA-256 `2e68239d9eb20fdc921630d4329823ad7c6535f56f61f69d4e314364ccce2eae`
- `ECON03c5_Host_Receipted_Regional_Commands_2026-11-01_Engineering_Report.md`

## Next gate
Bind region-scheduler commands to the authoritative `PersistentWorld` inventory and named NPC logistics with **one transaction or an explicitly reconciled effect journal**. A test-only `carrier_receipt` identifier must not be accepted as real proof. Active/inactive-region promotion must replay all due events before detailed simulation resumes. Keep distant calculations event-driven, without per-NPC ticks. GitHub remains an archive/source repository, not the test environment.
