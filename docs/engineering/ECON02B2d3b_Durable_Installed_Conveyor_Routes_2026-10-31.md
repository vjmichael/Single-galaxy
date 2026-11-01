# ECON-02B2d3b — Durable, material-backed spatial conveyor route installation

Date: 2026-10-31 (local Linux development)  
Parent source milestone: ECON-02B2d3a (`081effcfae5120a5129ebfefd0886c5a7de62eaa`)  
Scope: Versioned physical route installation at already-built outposts; **not** a GUI or true 3D conveyor rendering system.

## Implemented

- New host-authorized `Action::PlaceConveyorCell` and `Action::CompleteConveyorRoute`, appended without renumbering old actions. Both use the existing authenticated colony-ship actor, owning outpost, in-system proximity, and command-id idempotency checks. A cell command carries the pre-existing constructed link ID in `amount` and grid coordinates in `thrust_x`/`thrust_y`. Activation uses the link ID with zero coordinate fields.
- An already module-built directed link can now become a **site-local physical conveyor route**, comprised of orthogonally adjacent grid cells, including bends. The authoritative `FactoryLayout` engine checks the completed endpoints, item-compatible machine ports, occupied footprints, route crossovers, shared tiles, and graph cycles. A malformed path is rejected without partial application.
- Each accepted conveyor cell consumes exactly one **already fabricated construction module** (two ore-equivalent material units) and contributes the same embodied amount to the conserved world total. The link's earlier creation module remains separate and is not charged again on route activation. The one-module-per-cell quantity is an initial **game-balance prototype**, not an immutable realism rule.
- A link created when both endpoints are already spatially installed requires a complete route before it moves items. Adding cells to a previously valid logical legacy link also pauses it until completion. Links already created under older snapshots without spatial installation remain valid as legacy logical links, preserving backward-compatible saves and tests. Empty sources and full destinations still stop transfers; existing item filters, rates, and inventories remain authoritative.
- Incomplete routes survive SQLite save/restart and cannot move inventory. Completed routes resume their logical transport on the activation tick. `OutpostStorageLink` stores required/ready flags and a deterministic ordered cell path.
- Snapshot v30 adds canonical `factory_conveyor|site|link|ready|x:y;x:y;...` records alongside v27+ `storage_link` records. Versions 1–29 remain readable; a v29 world acquires v30 only when using the new route commands or constructing a link between already placed endpoints. Corrupt paths (orphaned links, impossible geometry, tile collisions, bad framing) are rejected by snapshot restore.
- SQLite `conveyor_cell_construction_t` receipts match real committed command identities, locations, and two-unit embodied material transfers; forged ledger endpoints are rejected. The existing durable effect/checkpoint journal remains the sole authoritative history, not the UI blueprint.

## Verified locally

- All-target Linux Debug compile: PASS.
- Dedicated `galaxy_econ02b2d3b_spatial_conveyor_tests`: PASS. It covers actual multi-corner grid routes, required-but-inactive links, unauthorized attempts, malformed routes, invalid cells with no material consumption, segment module costs, unfinished-route persistence, route activation, throughput, exact material conservation, remote/nearby step parity in a fixed-event scenario, mid-checkpoint fault rollback, corrupted route-snapshot rejection, and forged SQLite construction-ledger rejection.
- Native non-live CTest: **57/57 PASS**, Node.js: **84/84 PASS**, Python design: **3/3 PASS**.
- Full native CTest including legacy live/relay scenarios: **81/81 PASS** in a bounded run (four workers, per-test timeout 18 seconds). A second post-test-tweak full run is recorded separately where available.

## Clear engineering boundaries

1. The route system is a **world-state/grid simulation**, not yet an interactive visual construction GUI. It does not produce conveyor meshes or animate goods, and there is no path-placement HUD.
2. The original pre-spatial logical transport links remain readable and operable. Migrating/retiring legacy bypasses in new-game deployments needs an explicit compatibility policy and acceptance tests.
3. One installed conveyor route is limited to 4096 cells for safe per-command/state validation; users can eventually chain separate transport segments to span larger installations. This is not a factory land-size cap.
4. No generic blueprint-to-resource-backed construction work-order engine yet; authorized cell commands create completed route segments, not staged floor/wall ghost plans.
5. Advanced Tier 2–5 installed machines, independent finisher spatial identity, arbitrary junctions, player-saved construction templates, per-port direction/rotation, and user-guided/automated NPC routing are still upcoming.
6. Remote fast-forward for active dynamic production networks with changing conditions remains future work; only deterministic fixed-event parity is covered here.
7. Windows-native and full graphical playthrough acceptance remain open gates. GitHub is the source/milestone repository, not the development hub.

## Next gate

**ECON-02B2d4:** bind saved spatial factory blueprints to material-backed, staged construction plans, including placement preview, authorization, rollback, and cancellation. In parallel, build a minimal player-facing layout editor that issues these host commands rather than directly editing authoritative snapshots.
