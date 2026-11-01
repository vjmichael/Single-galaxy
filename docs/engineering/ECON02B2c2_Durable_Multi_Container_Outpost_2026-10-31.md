# ECON-02B2c2 — Durable multi-container outpost inventory

Date: October 31, 2026 · Linux native engineering increment
Parent: ECON-02B2c1 commit `bb69128461eb7645727b0ce85a0bf0d671533cb3`.
Status: **Persistent extra containers integrated and locally verified. Automatic conveyor transport still open.**

## Implemented

- Added two explicit player-host-authorized actions, `ConstructStorageNode` and `TransferStorageNodeStack`. A valid founding colony ship must own the actual constructed outpost and be close enough to use it, as in prior storage commands. A non-owner, non-colony ship, nonlocal actor or missing site cannot create/operate a container.
- `ConstructStorageNode` uses **one actual manufactured construction module**, creating a separate 4,000-litre (4 m³) capacity item container with a stable positive site-local numeric ID. The original B2b container remains node 0 and is unaffected. Duplicate construction IDs are refused. Numeric IDs are bounded only by the command's 32-bit routing representation; there is **no arbitrary eight-container gameplay limit**.
- `TransferStorageNodeStack` supports source/destination pairs ship↔node and workshop↔node, all-or-nothing in integer item counts. Its routing code is `thrust_x = 4*node_id + item_index`, with the four existing workshop materials indexed 0 aluminum, 1 panels, 2 frames, 3 scrap. `thrust_y` is 0 ship→node, 1 node→ship, 2 workshop→node, 3 node→workshop. The shared B2a `StackStore` validates *each node's* capacity before any world state changes. No item moves if the source is short or destination has inadequate free volume.
- Snapshot **v26** adds `storage_node|site|node_id|aluminum|panels|frames|scrap` lines, requiring a legitimate pre-existing base storage at the same site. Snapshot restore rejects invalid IDs, negative or over-capacity stacks, duplicate node definitions, or orphaned nodes. Older snapshots v1–v25 continue to load without invented new containers. Existing SQLite **schema 17** is retained, with two new ledger reasons and explicit source/destination reconstruction from host journalled commands.
- Every successful new construction or transfer has its own SQLite resource-ledger entry (`storage_node_construction_t`, `storage_node_stack_transfer_kg`) whose quantity, command, tick and endpoints are checked against independently committed command/event rows during recovery. The expected-ledger counts include both actions; removing, forging or misrouting one is rejected. Existing GRSM physical incidents inherit the real resource-ledger movement source and destination.
- Global `total_material_kg()` now accounts for one embodied two-tonne module equivalent per extra container and all its carried items. Containers have independent 4m³ limits: total outpost container capacity is **not** limited to 4m³ once multiple units are built.

## Verification

- Linux CMake/Ninja Debug all-target build: **PASS**.
- Complete non-live CTest native/integration: **51/51 PASS**.
- JavaScript tests: **84/84 PASS**.
- Python design tests: **3/3 PASS**.
- New `galaxy_econ02b2c2_persistent_nodes_tests` exercises multiple real module-built containers, duplicate refusal, non-owner denial, missing node denial, exact quantities in separate inventories, independent capacity across nodes beyond the original single-bin 4m³ limit, snapshot v26 canonical round trip, SQLite restart, unchanged global conservation, and modified destination ledger rejection.
- Previous B2b storage test still passes against the identical game simulation engine. The complete legacy live and native Windows/graphical playthrough suites were **not** verified in this increment.

## Incomplete / next gates

1. The new `LogisticsNetwork` is still a standalone *domain* component. Actual site containers do not yet expose host-owned, persistent automatically moving routes. This is the next **ECON-02B2c3** milestone: material-backed conveyors/ports, player-authorized directed links, item filters, rate limits, host-scheduled movement and backpressure using the existing per-node state. Do not load a value-copy of inventories, advance it and assume it has safely updated the authoritative world; proper journaled effects or deterministic replay are mandatory.
2. Current site-local IDs and 4m³ bins are logical physical game entities, not independently placed 3D coordinates or visual meshes. A future spatial construction UI must provide real placement, collision checks, rights and physical connection paths while imposing **no artificial overall factory-size limit**.
3. Only the four manufacturing item codes are presently routed by the new player command. Versioned external catalogue item-code contracts and arbitrary recipe items remain for later integration.
4. Cross-site hauling, NPC logistics orders, multi-link remote fast-forward under simultaneous route competition, Windows-native acceptance and complete GUI playthrough remain open.

**Game-design invariants:** Storage capacity is in gameplay cubic metres, not payload mass; recipes require their full minimum inputs and create neither output nor scrap below that threshold. Every constructed object and every material transfer must have actual resources and an authorized path. GitHub is the version-control and evidence record, not the development hub.
