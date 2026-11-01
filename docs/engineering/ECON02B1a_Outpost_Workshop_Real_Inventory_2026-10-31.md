# ECON-02B1a — Persistent outpost workshop, named operator, and finite cargo

Date: 2026-10-31 (local prototype)
Repository baseline: main commit `3a4f4304708093587d20819dfae205194cd42533` (GRSM-04b.B2a + ECON-02B0).

## Implemented

- New `ConstructWorkshop` action installs one Tier 1 panel processor at an **existing, player-owned, physically reached outpost**, consuming one *real* construction module (2 tonnes ore equivalent). A missing module/invalid site or unauthorized caller does not install anything.
- New `TransferWorkshopFeed` moves aluminum between the owning colony ship's inventory and the outpost workshop. This prototype uses one-tonne shipment increments, preserving the legacy whole-tonne cargo contract.
- New `AssignWorkshopOperator` selects one specifically identified, living, on-site habitat NPC belonging to the owning ship. An unauthorized or nonlocal crew identity cannot run the machine.
- The machine consumes a recipe minimum of 20 kg of actual aluminum per completed five-tick batch and produces 19 kg finished aluminum panel material **plus 1 kg recoverable physical scrap**. All quantities and batch progress are integer tracked. If less than 20 kg is available for a *new* batch, the machine consumes none of it and produces zero finished output and zero scrap.
- Requires a real outpost utility facility with available power and nonzero condition, plus the designated operator. Input, output and scrap each have finite one-tonne capacities; finished-goods backpressure stops later batches.
- Input is reserved as durable work-in-progress, preventing duplicate withdrawals and preserving the partially completed batch on recovery. There are no free goods produced on restart.
- New `TransferWorkshopOutput` collects a consolidated one-tonne pallet of 50 completed batches (950 kg panel product + 50 kg scrap). The bundled pallet is an interim compatibility constraint: the legacy ship cargo accounting requires whole-tonne transfers. Later precision cargo logistics should permit individual product / scrap movements.
- Snapshot format is `galaxy-kernel-v22` for workshop states. Existing old snapshots migrate without inventing factories or operators. Save/restore validates outpost ownership, operator presence, machine count, capacity limits, and reservations.
- The resource ledger records construction, aluminum shipments, and finished-goods transport with command/effect linkage. Physical mass is counted in `total_ore()` and checked against durable commits. New action numeric identifiers are appended to the existing enum to retain old action values.

## Real local acceptance evidence

- Build: `cmake --build /mnt/data/work/build -j4` — PASS on local Linux.
- Standalone integrated test: `ctest --test-dir /mnt/data/work/build -R 'econ02b1a' --output-on-failure` — PASS.
- All non-live native CTest suites: **45/45 PASS** (`ctest -E '_live_|_relay_|_cli_e2e'`).
- JavaScript/Node: **84/84 PASS** (`node --test tests/*.js`).
- Python design tests: **3/3 PASS** (`python3 -m unittest discover -s tests/design -p 'test_*.py'`).
- Full live + native E2E / Windows / actual GUI factory designer: NOT verified this slice.

Integrated test exercises: authorization failure; real outpost construction and material depletion; building powered utility and habitat; local operator required; initial no-input failure; mined silicate -> plagioclase -> aluminum; one-tonne actual transfer; first WIP on assignment; independent SQLite `recover_copy` of partial batch; 50 completed batches with mass conservation; exhaustion producing no phantom output/scrap; second input shipment and full output buffer stopping processing; pallet loading onto real ship; resumed production; snapshot tampering of machine structure and reserved material rejected; SQLite close/reopen and recovery copy with preserved inventories.

## Important limitations / next gates

- This is a **single fixed-recipe processor at an outpost**, not the fully general recipe/workstation/layout placement system. The `QuantifiedLine` state kernel is used for each tick, but the whole persistent world still executes ticks individually; there is **no true event-jump fast-forward for the full world** yet.
- Storage is not yet a separately placed physical container entity; its finite capacities live in the workshop record. Conveyors, utility machines, network flow, shared line scheduling, machine tier upgrades, physical machine collision/clearance and player-designed ghost rooms remain future work.
- Materials ship in tonne-compatible lots because the inherited cargo abstraction needs finer-grained kg support. This is deliberately conservative, not the final logistics design.
- Current workshop production completion is part of the serialized simulation state, **not a separate append-only named NPC execution ledger entry** for each automatic batch; the command/event ledger handles explicit installation and transfers. Do not infer autonomous NPC decisions or historical culpability from automatic production yet.
- Next: authoritative kg-precision transport, a second machine and bounded intermediate buffer, explicit batch effect receipt with operator provenance, delegated scheduling, and independently verified remote production fast-forward.

Local development and verification first. GitHub is repository and verification record only.
