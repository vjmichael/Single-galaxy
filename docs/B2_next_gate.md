# B2 — Next Work Package: Durable Persistence and Conservation
**Status:** NOT STARTED; engineering plan based on B1 and EVEmu P4 lessons.

## Primary outcome
Keep one star system and add persistent state across shutdown, restart and failure, without creating a path for item/currency duplication. This gate does not expand the galaxy footprint.

## Proposed tasks and tests

| ID | Implementation | Required test/evidence |
|---|---|---|
| B2.1 | Embed SQLite and schema migrations, do not require users to install MariaDB | Fresh DB create, schema version, migration and rollback |
| B2.2 | Persist galaxy/system/ship/asteroid state and RNG/tick/next IDs | Stop and restore; canonical in-memory replay matches restored state |
| B2.3 | Durable command IDs and execution journal with unique constraints | Duplicate command across process restart cannot reapply |
| B2.4 | Transaction coordinator stages all inventory and world changes, commits DB, then publishes memory/events | Inject fail before commit; DB and live state unchanged |
| B2.5 | Serialize/validate/version snapshots (distinct from B1 debug dump) | Corrupt snapshot rejected clearly; recover from last valid state |
| B2.6 | Deterministic checkpoint/recovery logic | Restart after interrupted command; one-or-zero application, no partial allocation |
| B2.7 | Resource ledger with creation/destruction cause codes | Sum ore+stored cargo+depleted/sinks balanced across steps |
| B2.8 | Native Windows x86-64 build and test, including SQLite packaging | Visual Studio Release build, tests, actual executable and restarts on Windows |
| B2.9 | Run a repeated stress/failure test including large command-ID set | No duplicate processing, no negative inventory, bounded memory plan |
| B2.10 | Instrument diagnostics and performance counters | Identify exact failed command, tick, entity, transaction and recoverability |

## Critical design requirement
Do not repeat EVEmu's reported hazard where a database transaction rolls back while an already-mutated inventory object remains changed. Construct the proposed full next state or staged delta, validate it, write it transactionally, and only publish it to the live world after commit. On DB failure, discard the staged delta; never rely on "the database rolled back" as evidence the live world was restored.

## Conditions
Keep this work isolated from EVEmu, the historical EVE client and Qwen's read-only client investigation. No GitHub push unless separately authorized. Clearly separate unit tests, SQLite integration tests and real Windows runtime results.
