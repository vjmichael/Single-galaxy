# ECON-02B2c1 — Directed, rate-limited multi-container logistics domain

Date: October 31, 2026 (local Linux engineering increment)
Base: ECON-02B2b / persistent constructed storage / snapshot v25.
Status: **Reusable domain engine implemented and tested. World integration is not complete.**

## Purpose

Provide a small deterministic item-transport contract before adding versioned player-built storage IDs and automatically advancing conveyors in the authoritative outpost world. `LogisticsNetwork` owns independent finite-capacity inventory nodes and filtered directed links using the EXISTING `inventory::Catalogue` and `StackStore` item definitions. This is game-volume-only storage, not density, mass or payload physics.

## Implemented source

- New `include/galaxy/InventoryLogistics.hpp` and `src/InventoryLogistics.cpp`, linked into `galaxy_kernel`.
- `create_node(id, capacity_liters)` requires positive stable ID, bounded positive capacity and no duplicates. Nodes are finite-volume mixed-item `StackStore` values.
- `add_route(id, source, destination, exact_catalogue_item_code, max_units_per_tick)` requires existing distinct endpoints, a valid stable item code and positive per-tick throughput. A source can feed several destinations; shared stocks are served in ascending route-ID order, deterministically.
- Routes move **only their configured item**. `advance(ticks)` invokes exact all-or-nothing `StackStore::transfer_to` operations using the minimum of available material, receiving capacity and allowed per-tick throughput. An empty source or full destination produces zero transfers, with no scrap, resource consumption or free material creation.
- A route can be enabled/disabled. Adding a directed cycle is rejected for this early factory-line contract. This is an initial safe topology restriction, not an arbitrary factory-size limit: future logistics can support more complex loops with precise ordering/deadlock semantics.
- Remote simulation uses arithmetic O(1) advancement for a single route and fast-forwards quiescent multi-link graphs. While the network is actively moving through multiple links, advancement currently performs deterministic tick-by-tick transfers; full event-driven multi-link aggregation is a future performance gate.
- `total_stacks()` provides cross-container item accounting with overflow guards; the test verifies results match when time advances all at once or one tick at a time.

## Security and authority boundary

`LogisticsNetwork` is a **domain value model, not a client-facing construction API**. Its `create_node` and `seed_from_authoritative_inventory` are called by the test only. Until a host-owned world adapter validates real outposts, construction materials, ownership, player/NPC work, physical placement and journaled stock transfers, this new type must NOT be exposed as a player or DLC command. In particular, `seed_from_authoritative_inventory` is NOT a gameplay resource grant. No in-world containers, persisted conveyor placement, NPC routing or SQLite records are created by this increment.

## Local verification (final source)

- Debug Linux full compilation: PASS (initial all-target build plus header hygiene rebuild).
- B2c1 directed logistics native test: 1/1 PASS.
- Full **non-live** native CTest: **50/50 PASS** (includes the new domain test and previous workshop/storage tests).
- JavaScript: **84/84 PASS**.
- Python design: **3/3 PASS**.
- The complete live native suite did NOT finish within the available execution window. This is not evidence of a live-suite pass or a Windows-native pass.
- Network tests cover empty source, full destination, mixed materials with exact filters, two-link line, branching priority, single-link billion-tick fast-forward, checked tick overflow, duplicate/missing nodes, rate guard, cycle rejection, enable/disable and mass/stack conservation.

## Next acceptance — ECON-02B2c2

1. Define versioned item IDs and durable storage/route identifiers instead of the original one-container-per-outpost key.
2. Add host-validated construction of multiple containers using real construction modules, with physical site ownership and within-site placement bounds; avoid arbitrary factory footprint caps.
3. Add commands to connect physically compatible ports, configure item filter and rate, and authorize/disable links, then bind them to real inventories without copy/mint semantics.
4. Record every scheduled conveyor movement as a durable replayable effect or host-authoritative deterministic tick outcome with auditable physical resource provenance. Ensure SQLite journal/ledger recovery rejects forged endpoints, quantities, and route provenance.
5. Validate backpressure across actual two-machine production, depleted materials and full intermediate/finished bins with no output and no scrap below recipe batch minima.
6. Test save/load, partial tick interruption, multiple producers/consumers, off-screen advancement parity, Windows native compile and eventual GUI factory editing.

GitHub remains the source repository, documentation and milestone record, not the primary development hub. Do not claim that the domain engine is a playable conveyor system yet.
