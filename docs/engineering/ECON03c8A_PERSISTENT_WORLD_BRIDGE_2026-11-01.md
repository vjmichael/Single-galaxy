# ECON-03c8a — Real PersistentWorld/Regional Ownership Bridge

**Date:** 2026-11-01. **Status:** Locally implemented and focused-Linux-tested against the archived full-game ECON-02B2d4c2 source baseline. This is the **first of several** ECON-03c8 tasks, not full live/cold state reconciliation.

## Integration scope

Before c8a, ECON-03c7 was an isolated SQLite regional ownership kernel with a host-supplied stage callback. c8a compiles that regional library alongside the real `galaxy_kernel` (`Simulation`, `PersistentWorld`, `Store` and its saved-game SQLite schema), plus a new `PersistentWorldBridge`.

The bridge deliberately scopes to **one entire PersistentWorld database** and **one region**; this is a safe vertical slice, not scalable multi-colony partitioning. It uses the original game's actual saved `snapshots` row and runtime `Simulation` state. It validates latest stored generation, exact canonical bytes, successful `Simulation::from_snapshot` restoration, state hash, resource and credit totals. It stages the exact source snapshot into `sg_world_detail_anchor` in the **same SQLite transaction** as the c7 ownership commit.

An opt-in `sg_world_region_gate` and SQLite trigger on new `snapshots` rows block `PersistentWorld::Store::commit` while the regional owner is cold or pending. Thus ordinary real-world commands/ticks cannot write another physical snapshot while that region has authority; failed snapshots do not alter the in-memory published `Simulation`. This protects the original authoritative inventory without copying it into the cold scheduler.

`enroll` accepts only an empty cold scheduler (no colonies, queued events, notices, shipments, or escrow), so no duplicated stock is created. Cold->detailed promotion stages the real saved world and transfers authority atomically. Detailed->cold demotion **only succeeds if the full saved world snapshot is byte-identical to the staged one**. If a real mining command or any other tracked game state changes, demotion rejects and the pending intent is aborted with generation bump. There is also an explicit `abort_pending` API for a crash after durable prepare.

## What this does NOT provide yet

- **No live resource transfer or event reconciliation** between nonempty populated regional colonies and active-world inventories. Deliberate fail-closed behavior is preferable to inventing or duplicating stock.
- No independently validated NPC cargo-loading/unloading receipts or actual ship routing. The existing c6 custody manifest remains regional-only until linked to the game's physical outcomes.
- No automatic fidelity switch based on proximity. `RegionalInterestManager` is still advisory, while c8a only handles authoritative ownership for a whole game world.
- No per-colony/multiplayer partitioning, multi-worker lease/cancellation integration, or full authorization of social-only writes. The snapshot trigger gates primary physical state commits, not all independent metadata mutations or arbitrary direct database access.
- No acceptance yet against the later local ECON-02B2d18d source/production schema or the Windows graphical client. The complete late source archive was not present; 4c2 is the last locally available **full-game source ZIP**. The newer c2–c7 regional modules were recovered and linked directly.
- No full-world cold population/production accuracy or graphics LOD renderer. The restored 1960s–1980s architecture direction is a separate art design record.

## Local acceptance

Release CMake/Ninja targeted integration build succeeded. Native CTest **8/8**: the real-world c8a test, original persistence test, six c2–c7 regional tests. Node **93/93**; Python design **3/3**. Targeted c8a additionally checks:

1. Real seeded game, cold enrollment, refused physical tick and command while cold, no change to game snapshot/generation.
2. Cold→detail→cold no-effect roundtrip with durable same-transaction snapshot staging; restart and re-enrollment without resetting owner generation.
3. Real successful `Mine` plus step in detailed mode, physical cargo conservation, fail-closed refusal to demote a changed world and automatic abort of pending intent.
4. Wrong live-world handle/source mismatch, same-game scope collision, refusal to import a populated regional inventory and explicit prohibition of material duplication.
5. Interrupted prepare/restart, explicit host abort, injected SQLite failure during detailed snapshot staging, total rollback of anchor, and successful later retry.

These tests prove a conservative boundary; they do not prove real hauling or cold resource/production progression. Keep this distinction in milestone status.

## Next c8b gate

1. Implement named, host-attested physical transfer receipts and route/carrier ownership from the actual `PersistentWorld` command and incident ledger, not text-only carrier identifiers.
2. Map active named colony resources to regional storage using one owned SQLite transaction or a validated durable effect journal, with explicit unit conversions, disjoint custody and monotonic effect sequence.
3. Reconcile *changes* in the detailed world at demotion: unresolved work, cargo in flight, consumption, production and NPC movements, with no duplications/losses and failure-injection restart tests.
4. Replace global-world fencing with independently scoped region actor leases, then bind multi-observer interest to durable mode changes. Profile on consumer Windows hardware; do not depend on GitHub CI.
