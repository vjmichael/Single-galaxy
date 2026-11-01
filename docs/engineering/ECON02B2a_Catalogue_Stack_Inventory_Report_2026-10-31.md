# ECON-02B2a — Catalogue-backed metric item stacks and atomic outpost transfers

Date: 2026-10-31 · Linux local prototype
Baseline: ECON-02B1c / persistent `galaxy-kernel-v24` snapshot.

## Scope completed

- `include/galaxy/Inventory.hpp` and `src/Inventory.cpp`: a reusable *gameplay-volume* item catalogue and `StackStore` value type. Entries have stable item codes and integer **litres per item unit**, with 1,000 litres = 1 m³. No payload mass, density, structural loading or extra flight rules.
- `Catalogue::standard()` carries every currently recognized legacy geological mineral, manufactured good, physical residue, ore, refined material and construction module. Old balancing values are preserved: ore and refined count as 1,000 litres/unit, modules as 2,000 litres/unit, and existing kg-counted materials as 1 litre/unit. Catalogue permits alternate item volumes for later gameplay balancing but persistent worlds remain pinned to the standard catalogue until a versioned migration is developed.
- `StackStore` validates item codes, duplicate catalogue definitions, negative quantities, overflow and finite-volume capacity. An all-or-nothing `transfer_to` rejects insufficient stock or full destinations without changing either store. It prepares the destination map entry before debiting the source to avoid an allocation-failure partial transfer.
- The actual `TransferWorkshopStack` command now uses identical catalogue-backed inventory views for the ship and the workshop bin, transfers exact item quantities on value copies, then commits only the already-validated item and quantity to durable world fields. All original proximity, actor authority, NPC construction, resource journals and persistence machinery remain in force; the game cannot teleport inventory or generate free matter through the shared API.
- `Simulation::ship_inventory_snapshot` and `workshop_bin_inventory_snapshot` expose read-only item quantities, capacities and occupied volume for future cargo UI, storage equipment and logistics planning. These are **copies of authoritative physical state**, not alternative stores that may be mutated independently.
- No snapshot/schema migration is required because persistent fields and command encodings are unchanged (still v24). Prior v1–v24 world migrations remain readable. Old pallet commands remain available temporarily.

## Local verification

- CMake/Ninja Linux Debug build: passed.
- Full native/integration CTest: **73/73 passed**, including the preexisting live relay suites and the new `galaxy_econ02b2_inventory_tests`.
- Node.js tests: **84/84 passed**.
- Python design tests: **3/3 passed**.
- Targeted retest after exception-safety hardening: **4/4 passed** (B1a, B1b, B1c, B2a).
- New tests validate non-uniform catalogue volumes, arithmetic overflow, full bins, insufficient input, catalogue mismatches, zero/negative quantities, duplicate IDs, exact-transfer conservation, and ship cargo snapshot compatibility. Updated ECON-02B1c integration checks live inventory views against the authoritative transferred metal and panel stock after real outpost commands, plus its earlier SQLite restart/tamper proof.

## Boundaries / ECON-02B2b next gate

This is the **first part** of ECON-02B2, not a completed freeform player factory, newly placed warehouse or graphical cargo system. Existing workshop bins are fixed per-kind (1,000 units) while the shared inventory primitive already supports mixed stacks. Persistent generic container entities, world-authorized container construction, placement, container-to-container transfers, physical conveyor/link capacities, throughput/backpressure and advanced NPC logistics remain unimplemented.

Prioritize a **single real outpost container** with finite mixed-item m³ storage, durable identity/ownership, atomic ship↔container and workshop↔container transfer commands, and recovery/tamper tests. Then connect two installed production machines via bounded logistics links, without introducing artificial floorplan caps. Preserve exact-batch prerequisites: below-minimum ingredients -> no input consumed, no output, no scrap. A machine stops when any required output or scrap destination is full.

GitHub remains the repository and milestone record only; development and testing are local-first.
