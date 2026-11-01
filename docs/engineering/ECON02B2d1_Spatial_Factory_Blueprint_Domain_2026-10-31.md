# ECON-02B2d1 — Spatial factory blueprint and routing foundation

Date: 2026-10-31 | Parent: ECON-02B2c3 `3f65499b55e0411883c921a1eba46f6350cd2175`

Status: Local C++17 domain implementation and tests. **NOT an interactive 3D conveyor/construction system.**

## Purpose

The c3 version has real durable, module-built outpost storage links and finite-resource transfers, but only logical endpoint IDs. To support unlimited player-authored factory footprints, the game needs a portable spatial plan and routing validator that an authoritative host can use before accepting physical construction.

## Implemented

- `include/galaxy/FactoryLayout.hpp` and `src/FactoryLayout.cpp` add a spatial factory blueprint domain backed by the existing `Catalogue` and `LogisticsNetwork`.
- Place storage with variable rectangular footprints and game-volume capacity; place T1–T5 generic machines with 2×2, 4×4, 8×8, 16×16, 32×32 grid footprints and **explicit input/output item ports**, without product-specific machine whitelists. Junctions can be splitters, mergers and single-item filters.
- Construct explicitly directed conveyor paths from neighboring source-device tiles to neighboring destination-device tiles. Each route identifies one catalogue item, positive transfer rate, unique ID and a continuous cardinally adjacent path. The path cannot collide with existing equipment, itself, or another route on the same ground layer. Cyclic directed routes are disallowed, matching c3 logistics constraints. Upgraded crossings, elevated layers and multi-item routing await future work.
- The blueprint can compile into a `LogisticsNetwork` with real catalogue-governed storage volumes, deterministic route priority, bounded transfer, exact item accounting, and backpressure. Compilation **does not seed materials, mint equipment, perform recipes, or grant world authorization**.
- Canonical textual blueprint export/import includes placements, item port declarations and conveyor paths. Import revalidates each placement/link, uses deterministic ascending IDs, and rejects noncanonical encodings and invalid geometry. These are **design data**, not durable authenticated SQLite construction receipts.
- No preselected factory room area, per-factory device-count ceiling or maximum building footprint; grid coordinate magnitude and maximum individual route segment length are strictly implementation safety limits. Long routes can be subdivided into multiple links and junctions.
- No density, payload mass or physical stress computation. Cargo volumes use game-defined m³.

## Local acceptance

- Complete Linux Debug all-target compilation.
- Targeted `galaxy_econ02b2d1_factory_layout_tests`: equipment tiers, arbitrarily distant placement, canonical plan round-trip, geometry and item-port validation, splitter/filter routing, rejection of collisions/overlap/cycles/malformed data, atomic failed-authoring behavior, conserved multi-route transport and distant-vs-stepwise equality. One-route billion-tick arithmetic fast-forward with volume backpressure is also tested.
- Complete non-live native suite: 54/54; JavaScript: 84/84; Python design: 3/3.
- Full Linux native CTest: the first broad attempt stalled, but a randomized-schedule bounded retry finished successfully with **78/78 passing**, including older live and relay scenarios (`/mnt/data/work/b2d/ctest-full-random.log`).

## Open ECON-02B2d integration gates

1. **Authoritative placement and ownership:** player outpost command(s) must allocate/cancel construction resources, persist grid equipment placement and material-backed conveyor paths, preserve old saves, and reject unauthorized or physically impossible builds. Blank plans alone are free ghost data.
2. **Machine ports and actual work:** wire real `OutpostWorkshop` feed/output/scrap storage into the compiled transport nodes without copying inventories. Enforce recipe minima, actual output capacity and named operator assignments on real world state, not on a separate demo inventory.
3. **Utility and contention:** splitters, mergers, filters, stacked feeders, optional power/maintenance/staffing, and route sharing require exact, documented gameplay capacity rules and receipt-based accounting.
4. **Off-screen parity:** authoritative unattended factory scheduling must match near simulation for the same event/production schedule. The new layout's compile-to-network adapter is not a world-level remote scheduler.
5. **Player interface:** ghost-grid placement, rotation, snapping, blueprints, visual conveyors, 3D world placement, and NPC-generated valid templates remain to implement. Gameplay should not impose compulsory factory micromanagement.
6. **Platform:** Windows-native and complete graphical acceptance remain open.

GitHub is a source repository and engineering record, not the primary development host. This work is made locally and should only be described as a source-tested spatial blueprint foundation.
