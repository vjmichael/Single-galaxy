# DEMOG-04b2a — Canonical NPC faction roster projections

**Status:** locally implemented C++17 prototype increment; **not** integrated into production PersistentWorld. November 2, 2026.

The prior DEMOG-04b1 expedition SQLite ledger now has faction memberships keyed by the canonical named-NPC ID, read-only faction roster queries JOINing the single canonical custody row, a per-NPC monotonic state revision, and an atomic event journal. SQLite triggers append custody-change events in the same transaction as reservation, departure, arrival, cancel or destruction. Roster consumers can poll events by cursor and reconcile missed notifications against canonical state; delivery workers and UI have **not** been implemented. Cached roster statuses must not authorize another mission.

**Local test:** one NPC belongs to three factions; reserve/depart/arrive states match across all three roster reads, including pre-commit fault rollback and database reopen. DEMOG-04 selected native CTest: **1/1 passed**. Full game regression suite, concurrency race suite, ASan/UBSan for this increment, and cross-platform runs have **not** been performed.

**Source:** conversation artifact `SingleGalaxy_DEMOG04b2a_Canonical_Faction_Rosters_2026-11-02.zip`, containing the incremental C++ patch, three changed files, build log and report. The patch is **not merged to remote main**.

**Next DEMOG-04b2b:** replace prototype-only NPC registry ownership with production `PersistentWorld` / ART-02b1 identity and family records, verify real ship and cargo custody, and ensure one authoritative headcount. Add event subscriber/reconciliation, multi-process concurrency and cold/warm save/restart tests before NPC auto-launch.

See [roster design](../design/NPC_FACTION_ROSTERS_ASSIGNMENTS_AND_TRAVEL_STATUS_2026-11-02.md).
