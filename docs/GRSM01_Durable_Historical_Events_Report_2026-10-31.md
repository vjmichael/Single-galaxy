# GRSM-01 — Durable Historical Events

**Single Galaxy · 2026-10-31 · Local Linux engineering gate**  
**Status:** Implemented and verified in the offline C++/SQLite kernel; **not yet connected to UI, notoriety, beliefs or NPC speech.**

## Objective

The player's real actions and the simulation's resulting outcomes must be retained as historical truth, separate from what NPCs believe or later say about those events. These facts must commit atomically with physical simulation state and must never create a death, casualty, shipment, witness or achievement by inference.

## Implemented

- SQLite schema **user_version=5**, upgrading from v1–v4 through existing transactional migrations. Historical events begin at the schema-upgrade generation; older events are **not fabricated or retroactively inferred**.
- A canonical event row is written **only after a queued command actually executes**, keyed by `command:<id>` and linked to its exact command journal, tick, actor, target, action, outcome, transferred amount and world generation.
- Outcome `Applied` vs. rejection is recorded explicitly; a rejected mining command does not imply that ore changed hands or that anyone was harmed.
- Rows commit in the **same SQLite transaction** as receipts, physical resource ledgers, operation log and world snapshot. A failed/aborted step does not publish an event.
- `historical_events` rejects ordinary UPDATE and DELETE through database triggers. On reopen, every row is checked against the independent command journal and snapshot, and every post-migration executed command is required to have exactly one corresponding historical row. A compact deterministic FNV-1a checksum is an additional accidental-damage check, **not a cryptographic authentication mechanism**.
- `PersistentWorld::recent_history(limit)` gives a bounded (1–256), read-only retrieval API for future GRSM/APS consumers; it is not a reputation API.
- `PersistentWorld::recover_copy` preserves and validates the new records on a copied recovery database.

## Verified locally

- CMake/Ninja C++ build completed on Linux with system SQLite 3.46.1.
- **55/55** CTest native/integration tests passed, including a new `galaxy_grsm01_history_tests` case and updated genuine-v1 migration fixture.
- **84/84** Node JavaScript UI/contract tests passed (existing tests unchanged).
- New test scenarios: no record on submission only; successful ore transfer; rejected action; duplicate command refusal; forced commit failure/rollback; bounded query; restart; copy recovery; corrupted action detection; missing row detection; non-destructive v4 upgrade; physical mass conservation.

## Current limitations and next gates

1. Historical truth captures **executed command outcomes** only. It does not yet claim to capture every automatic event, NPC birth/death, witnessed incident, travel activity or physical casualty; those require specifically typed event integration and conservation assertions.
2. Notoriety, social memory, rumor, evidence provenance and faction-specific belief stores are **not implemented**. The truth ledger cannot be modified by their future dialogue systems.
3. No backfill of facts on legacy saves. The migration generation is recorded explicitly.
4. Restoring a world currently scans historical entries for consistency. Long-lived large-population simulations will need bounded checkpoint/proof strategies and paging without sacrificing correctness.
5. The historical digest is not cryptographic and is not meant to resist a privileged attacker who rewrites the SQLite database and all journals.

**Next recommended test — GRSM-01b:** typed, transaction-bound simulation facts for NPC losses, ship destruction, cargo changes and journey events (when those mechanics exist); keep strict physical conservation and exact causality. Then **GRSM-02:** provenance-aware competing claims and faction-specific belief, with explicit no-knowledge isolation tests.

## Repository policy

Build and test locally; use GitHub as source/version history and documentation rather than the primary development host. The ChatGPT Library `/Single Galaxy` folder stores handoff archives and source snapshots. A local checkout on the developer's PC or another persistent environment is needed for continuous engineering; this tool working directory is not itself a guaranteed permanent workspace.
