# ECON-03c8a — Real PersistentWorld ownership bridge (2026-11-01)

**Status:** Linux locally implemented and regression tested against the recovered **full-game ECON-02B2d4c2 source baseline** combined with the isolated ECON-03c7 regional library. This is an **initial integration**, not a full physical cargo/NPC/production reconciliation. The main branch has **not** been replaced.

## Actual integration

The new opt-in `PersistentWorldBridge` uses the real game's SQLite `snapshots` table and `Simulation::from_snapshot` to validate current committed physical state, generation, canonical serialized bytes, game-state hash and conserved ore/credit totals. A bridge entry in the same database fences normal `PersistentWorld::Store::commit` snapshot inserts while cold ownership or a transition is pending, avoiding a duplicate live writer. Failed snapshot commits cannot publish a mutated `Simulation`.

Promotion `Cold → Detailed` stages the **actual canonical world snapshot** in `sg_world_detail_anchor` and transfers region ownership with the same SQLite transaction. Demotion `Detailed → Cold` is deliberately allowed **only when the world snapshot is byte-identical** to its staged counterpart. Real mining in detail mode works; returning to cold mode after those state changes **fails closed** until a future effect reconciliation implementation exists. The bridge requires the cold regional scheduler to be empty when enrolling; it never clones spendable stock, NPC cohorts or cargo into a second simulation. One bridge owns an entire existing `PersistentWorld`; finer partitioning is future work.

An explicit `abort_pending` supports crash/restart recovery; injected SQLite failure during snapshot staging rolls back stage and owner flip, leaving the world fenced and recoverable.

## Local verification

- Release build linked real `galaxy_kernel` and regional subsystem: passed.
- Native CTest **8/8**: one new real-world integration test, original persistence test, six earlier ECON-03c2...03c7 regional regression suites.
- Node.js tests **93/93**; Python design **3/3**.
- New bridge tests cover cold writer rejection, no-effect roundtrip, restart, persisted snapshot/physical cargo identity, successful mining plus rejected unsafe demotion, wrong-game mismatch, scope collision, refusal to mirror populated cold inventory, interrupted prepare and staged-SQL-error rollback.
- Patch applied to the exact recovered 02B2d4c2 full-game ZIP, with **596 source files matching** a clean independently patched copy.

These are Linux-local test claims only. No Windows acceptance, later ECON-02B2d18d full source integration or graphical renderer test is claimed.

## Source artifacts (delivered separately in the conversation)

- `SingleGalaxy_ECON03c8a_Real_PersistentWorld_Bridge_Source_Tests_2026-11-01.zip` (596 files): SHA-256 `2d2d7abe62ddd0eade3a681f9b7c6e73109b6b4ea764cd16f69fe675b1a832f9`
- `SingleGalaxy_ECON03c8a_Real_PersistentWorld_Bridge_Changes_2026-11-01.diff`: SHA-256 `c02ffc57538c36d35f73750a170d7c8e315dc25fc28b2a16e27112b361503d68`
- Local engineering report: SHA-256 `eea5df3feb38ae7e11c70959c30eba83399a14e15c7ffc09d67151c1047aa92c`

**The GitHub record is not a claim these source artifacts have been pushed as repository source.** GitHub remains source/history repository, not development environment.

## Remaining 03c8 work

Prove NPC/carrier/route authorization from real physical receipts; reconcile actual changed inventories, work, population, scheduled transport, consumption and production at demotion; replace the whole-world gate with region-granular actor leases; link multiple-player distance interest to durable handoff; integrate with later real local game source revision and verify Windows. Nothing in this milestone implements graphics LOD or the complete 3D game.
