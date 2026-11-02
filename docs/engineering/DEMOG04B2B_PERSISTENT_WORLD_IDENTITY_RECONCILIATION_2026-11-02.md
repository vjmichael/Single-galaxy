# DEMOG-04b2b — PersistentWorld identity reconciliation preflight

**Status:** limited read-only C++17 preflight source compiled locally. **Not** the final authoritative transfer bridge.

An initial source-only `PioneerWorldReconciliation` module compares explicit NPC IDs against both `PersistentWorld::world().npc_records()` and `ExpeditionLedger::named_custody()`. It detects duplicate/zero IDs, identities absent from either store, already-reserved/in-transit/lost NPCs, and mismatched home-ship vs prototype origin. It makes **no changes** to either system, and does not authorize boarding.

The code was compiled locally with `g++ -std=c++17 -Wall -Wextra -Werror`. This is only a compile check; no executable integration tests were completed for this new bridge.

**Important:** The actual game NPC record has `id`, `home_ship`, `location`, `role`, farm XP and mentor ID. The prototype's `colony` must not be assumed equivalent to every world location. A proper integration must define origin mappings and authoritative ownership, reconcile named and aggregate headcounts, and preserve family/genetic records from the full world. Read-only preflight is not sufficient for a cutover.

Next: fixture-based tests with actual `PersistentWorld` saves and expedition ledger; then a single authorized transaction/effect journal for departure/arrival, not two independent writes. The prior faction-roster fanout stays prototype-only until that integration is proven.

## Runtime test update (November 2, 2026)

Compiled and executed a new C++17 native test against the existing local `libgalaxy_kernel.a` with `-Wall -Wextra -Werror`. A real `PersistentWorld` SQLite save with a 24-person individual workforce and its colony ship was matched to an independently seeded prototype expedition ledger. The test verified matching identities, duplicate IDs, missing ledger identity, missing world identity, and reopening both databases with the same audit result. **PASS**.

Evidence package: `SingleGalaxy_DEMOG04b2b_Runtime_Reconciliation_2026-11-02.zip`, SHA256 `d5937bec4886ab3662dbdb2f65f0acdcb848ea7d1ae87102e7fce976169ae768`.

**No authoritative cross-database transaction exists yet.** The test seeds the isolated ledger with IDs from the real world for audit purposes; it does not safely move real NPCs, family records, ships or stocks. Next engineering gate remains single-world-owned custody/effect journal, followed by atomic transfer/recovery tests and multi-faction fan-out integration.
