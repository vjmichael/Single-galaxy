# DEMOG-04b2b — PersistentWorld identity reconciliation preflight

**Status:** limited read-only C++17 preflight source compiled locally. **Not** the final authoritative transfer bridge.

An initial source-only `PioneerWorldReconciliation` module compares explicit NPC IDs against both `PersistentWorld::world().npc_records()` and `ExpeditionLedger::named_custody()`. It detects duplicate/zero IDs, identities absent from either store, already-reserved/in-transit/lost NPCs, and mismatched home-ship vs prototype origin. It makes **no changes** to either system, and does not authorize boarding.

The code was compiled locally with `g++ -std=c++17 -Wall -Wextra -Werror`. This is only a compile check; no executable integration tests were completed for this new bridge.

**Important:** The actual game NPC record has `id`, `home_ship`, `location`, `role`, farm XP and mentor ID. The prototype's `colony` must not be assumed equivalent to every world location. A proper integration must define origin mappings and authoritative ownership, reconcile named and aggregate headcounts, and preserve family/genetic records from the full world. Read-only preflight is not sufficient for a cutover.

Next: fixture-based tests with actual `PersistentWorld` saves and expedition ledger; then a single authorized transaction/effect journal for departure/arrival, not two independent writes. The prior faction-roster fanout stays prototype-only until that integration is proven.
