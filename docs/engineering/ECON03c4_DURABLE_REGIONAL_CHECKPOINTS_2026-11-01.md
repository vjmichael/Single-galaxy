# ECON-03c4 — Durable Regional Checkpoints (2026-11-01)

Baseline: local ECON-03c3, **not** GitHub main. Development and testing local; GitHub is archival only.

Implemented: SQLite-backed opt-in regional scheduler checkpoints, including logical tick, population cohorts, needs and inventories, all pending events, shipment reservations and in-transit cargo, shortage episodes, and notice outbox. Atomic SQLite transaction with optimistic revision check prevents stale writers overwriting progress. Versioned bounded binary snapshot, checksum for accidental corruption, and isolation by region scope. Scheduler exposes custody totals to verify conservation of materials reserved but not held in a building.

Verified: five focused native/regression tests passed; 93/93 JavaScript tests passed; explicit restart tests at ticks 0/19/40/100, including no duplicate delivery, notification acknowledgement persistence, stale-write rejection, corrupted snapshot rejection, and 1.3-trillion-resident cohort roundtrip.

Single-run local microbenchmark: 2,000 independent cold cohorts (2.6 quadrillion represented residents), 1 trillion logical ticks: advance 5.38 ms; SQLite save 15.12 ms; reload 12.81 ms; 757,760-byte database. **Not a whole-galaxy benchmark; no active rendering, combat, factories or real ship movement.**

Locally verified package: `SingleGalaxy_ECON03c4_Durable_Regional_Checkpoints_2026-11-01_Source_Tests.zip` (666 files), diff: `SingleGalaxy_ECON03c4_Durable_Regional_Checkpoints_2026-11-01_Changes.diff` (applies to exact ECON-03c3 source baseline). SHA-256 package: `192fdf0a5046664a21da30aff12312b7e10d336cd9a0cb0aff22f0b816c90594`. The full source and patch are delivered through the conversation; this GitHub document is a milestone record, **not a claim that those source bytes have been pushed**.

Open: authoritative colony-host integration, permissioned event submission, durable resource-transfer receipts, route/carrier validation, production/power dependencies, OS push delivery, active/inactive region reconciliation, Windows GUI acceptance, consumer-hardware whole-world benchmarks. Pending notifications are at-least-once until an acknowledging consumer deduplicates by (scope, sequence).
