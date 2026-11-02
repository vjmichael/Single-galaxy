# DEMOG-05g8 — Deterministic cold voyage scheduling prototype

Local native C++17 / SQLite test passed with strict warnings. Source bundle available separately.

Implemented isolated scheduler with transport IDs, ship IDs, origin/destination, departure and arrival ticks, one-active-voyage-per-ship index, atomic departure and arrival receipts, durable clock, restart, repeat-arrival and rewind rejection.

**Important scope:** Arrival means the *scheduler* has marked the voyage complete; it does **not** move canonical `PersistentWorld` ships or NPCs. The prototype is not yet wired to the regional bridge and is not a physical embarkation/disembarkation implementation. No claim of full NPC Space Transportation Simulation 2099 acceptance.

Integration blockers: canonical passenger custody action/manifest, aggregate headcount updates, world-owned receipt, ship position transition, one atomic effect journal, and cold-to-warm reconciliation. Keep the scheduler as a draft, not an alternate authority.