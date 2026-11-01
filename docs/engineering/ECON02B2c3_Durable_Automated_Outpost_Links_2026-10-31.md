# ECON-02B2c3 — Module-built automatic outpost transport links

Date: 2026-10-31 | Local Linux prototype | Parent: ECON-02B2c2, `d5b0a5eddded87607d5146ef15697c6c9bf56cd3`

**Status:** Implemented, locally verified; *not* 3D conveyor geometry or a full remote factory scheduler.

## Implemented

1. **Authorization and construction:** `ConstructStorageLink` is a player/host-authorized command for the owning colony ship at a physically reachable, player-built outpost with an existing constructed container. Each accepted link consumes one genuine construction module (2 old ore-equivalent tonnes embodied in the link), issues a committed execution receipt and physical resource-ledger entry, and receives a durable immutable route ID equal to the construction command ID. Nonowners, remote ships, unbuilt sites, non-existent endpoints, identical endpoints, duplicated identical routes, and links creating cycles are rejected. There is no artificial limit on the number of links; ID and checkpoint storage are technical limits.
2. **Directed, bounded transfer:** Routes have source/destination site-local storage IDs, an item filter, and a rate of 1–1,000 whole item units per simulation tick. Source/destination IDs are encoded as `Command.thrust_x` and `thrust_y`, and `amount = 4*rate + item_index`, where item_index is 0 aluminum, 1 panels, 2 frames, 3 scrap. Node 0 refers to the legacy player-built base container; positive nodes refer to earlier ECON-02B2c2 constructed containers. All have independent 4,000-litre (4 m³) storage capacity. The host advances each route in stable route-ID order using the canonical item-catalogue and `StackStore` transfer checks. All moved units are debited and credited in the same authoritative simulation step, not teleported or copied. A full destination, empty source, zero compatible inventory, or no free whole item volume causes zero movement.
3. **New snapshot v27 and SQLite reconciliation:** A snapshot `storage_link` record stores route ID, endpoints, item index, rate and cumulative transfer count. Newly committed `AutoStorageTransfer` activity records are durable with tick, originating site, route ID and quantity. Each automatic transfer writes a separate SQLite `resource_ledger` row in the same transaction as the authoritative checkpoint. On recovery, SQLite checks each recorded activity against the ledger's tick, amount, source and destination, and snapshots reject missing/duplicated/malformed links, cycles, invalid item/rates and cumulative movement counts that disagree with the recorded events. The new `storage_link_construction_t` ledger reason is checked against the authenticated command journal and effect receipt. Altered automatic transfer destinations fail closed.
4. **Compatibility:** Previous snapshot versions v1–v26 remain readable; no historical transport is invented for old saves. Existing workshop production, single-container and multi-container commands are preserved.
5. **Conservation and game design:** Cargo capacity is a gameplay volume in m³; **there is no payload-mass restriction**. Recipe minimum-input rules, finite output stores and scrap accounting are unchanged. A physical construction module backs every new link; future art/3D placement is not implied by this logical link.

## Local verification

- CMake/Ninja Debug all-target compilation: **passed**.
- Complete native CTest, including older live/relay scenarios: **77/77 passed** (`ctest --test-dir build -j4 --timeout 12 --output-on-failure`).
- Native non-live regression: **53/53 passed** (`ctest -E '(live|relay)'`).
- Node tests: **84/84 passed**.
- Python design tests: **3/3 passed**.
- `galaxy_econ02b2c3_auto_links_tests` explicitly exercises valid module-backed link creation, unauthorized construction rejection, non-existent endpoints, directed-cycle refusal, per-tick item rate, isolated item filtering, source depletion, full-buffer backpressure, conserved world material, snapshot v27 canonical round-trip, deterministic tick replay, SQLite reopen/resume, failure injection after checkpoint write (atomic rollback), and tampered automatic transfer destination rejection.

## Open follow-up gates

- **Visual placement and construction:** Node and link IDs are logical objects attached to an outpost, not 3D-positioned conveyor meshes, pipes, sorters or collision-checked routes. The player-authored freeform layout and NPC blueprint planner remain future work.
- **Power/maintenance/parts:** The first link is module-backed but runs passively in the prototype. Electrical load, wear, maintenance labor, splitters and filters with multiple item codes need their own rules and integration.
- **Remote fast-forward:** The authoritative integration executes ordinary simulation steps; it **does not yet use the standalone B2c1 network's aggregated multi-tick fast-forward** when the player is away. Never claim galaxy-scale scheduling performance based on this test. Multi-route contention and throughput competition need explicit remote-vs-near parity tests.
- **Event growth:** The prototype stores one activity and resource ledger entry per completed tick/route movement; compacted, hash-protected historical checkpointing is needed for indefinite unattended industrial play.
- **Platform and GUI:** Native Windows game acceptance, independently verified Chromium 3D gameplay, and physical route authoring remain open.

GitHub serves as source repository and milestone/evidence record. Source development and testing were performed locally.
