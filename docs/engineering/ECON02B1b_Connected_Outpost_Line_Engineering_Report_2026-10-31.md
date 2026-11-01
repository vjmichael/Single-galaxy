# ECON-02B1b — Two-stage quantified manufacturing at a real outpost

Date: 2026-10-31 (local Linux prototype)
Baseline: ECON-02B1a, persistent outpost Tier 1 workshop. This increment introduces a second independent machine; it is **not** a full spatial factory designer or a conveyor-item simulation.

## Delivered

- Added `ConstructWorkshopFinisher`, `AssignWorkshopFinisher`, and `TransferWorkshopFinalOutput` to the end of the action enum; older action IDs remain stable.
- A second machine is physically installed at an existing owner-controlled workshop for **one actual construction module** (2 metric tonnes ore equivalent); double installation and unauthorized attempts are rejected. Workshop embodied material is 2t without the finisher and 4t with it.
- **First processing stage (existing):** 20kg input aluminum, 5 ticks, 19kg aluminum panels + 1kg recoverable scrap. The local 1,000kg panel bin is now the second stage's finite intermediate buffer. The first machine retains its existing independent output-pallet action.
- **Second stage (new):** each complete 20kg batch of those panel intermediates is shaped in 5 working ticks into 19kg aluminum frames + 1kg recoverable scrap. No valid 20kg input batch means **no input consumption, no output and no finisher-generated scrap**. Final frames have a separate bounded 1,000kg physical store; both stages share a separate bounded 1,000kg scrap store.
- The finisher requires a **different, named on-site habitat NPC** from the manual first-stage operator. A removed operator pauses WIP without losing reserved material; reassignment resumes it. A real powered/maintained outpost utility is required, and each staffed working stage contributes to the utility-demand calculation.
- Downstream final-store blockage stops the finisher. Its intermediate panel bin then fills, so upstream processing stops and leaves unused aluminum in place. There are no phantom conveyor items or automatic stock teleports. The same quantified production kernel executes both stages.
- `TransferWorkshopFinalOutput` transfers 50 completed finisher batches as one conserved whole-tonne pallet: **950kg frames + 50kg recoverable scrap**, physically moving them to the colony ship subject to real cargo capacity. This is deliberately temporary until item-level kg-precision logistics is implemented. Extra upstream scrap remains at the workshop.
- Snapshot version **galaxy-kernel-v23** adds the finisher installation flag, NPC identity, WIP reservation and remaining ticks, physical finished stock, and counters. Earlier v22 snapshots remain readable. The SQLite journal/ledger records finisher construction and finished-goods transfer as independently validated command/effect-linked resource movements.

## Local verification

- CMake Ninja Debug full build: PASS.
- Dedicated ECON-02B1b real-persistence test: PASS. Old ECON-02B1a test: PASS.
- Complete non-live native CTest regression: **46/46 passed**.
- Node tests: **84/84 passed**.
- Python design tests: **3/3 passed**.
- The test covers genuine aluminum mining/refining, actual modules and outpost construction, two assigned local NPCs, denied unauthorized construction, prohibited same-NPC operation of two manual machines, subminimum downstream input with no finisher output or scrap, mid-finisher WIP SQLite recovery, withdrawal and reappointment of an operator, output backpressure, upstream buffer exhaustion while power and maintenance remain available, real final-goods cargo shipment, whole-world mass conservation, v23 snapshot round-trip, malformed WIP/body rejection, restart/recover_copy, and SQLite resource-receipt endpoint tampering rejection.

## Not yet implemented / deliberate boundary

- **No actual conveyor-belt objects, routing, transfer speed, pathfinding, placed floor grids, collision or player-saved factory layouts.** The panel buffer models a bounded co-located transfer, not distance-sensitive transportation.
- The second machine is fixed Tier 1, fixed shaping recipe, and hardwired to the original processor; this is not a general graph of arbitrary freely chosen machine operations and recipes.
- The whole persistent world still steps one tick at a time; it does not yet support vectorized event-driven advance across all world systems. The small `QuantifiedLine` domain supports aggregated advancement separately.
- Storage lives on the site-local workshop record, not as separately constructed crate or container objects. Machines currently share a scrap store by design.
- Whole-tonne shipment pallets are a compatibility constraint, not the intended item-level cargo design. A small quantity of panels or frames cannot yet be transported individually.
- Production completions are durable state, but lack their own *append-only individual effect event* with NPC identity for every batch. Do not use automatic workshop counts as an independently verified GRSM actor-culpability ledger.
- No complete live graphical playthrough, native Windows build, long-run performance/scale profile, off-screen accelerated world simulation, or distributed authoritative multiplayer has been proven in this increment.

## Next engineering gates

1. ECON-02B1c: authenticated **kilogram-precision physical goods handling** and independent transfer of panels, frames and scrap, without whole-tonne mixed pallets, preserving cargo/conservation and recovery invariants.
2. Make storage buffers and utility components first-class installed objects and bind material movement to bounded connector throughput, preserving physical placement and worker access when introduced.
3. Generalize recipe/machine stage selection, add persisted per-batch production receipts with named staff provenance, and verify against the physical event ledger.
4. Add a consistent event-driven distant-factory advance layer and comparative near/far/world replay tests before scaling factory counts.

GitHub is a repository and evidence record, not the development hub. Source and validation were performed locally on Linux.


**Final broader Linux acceptance (same source revision):** all **71/71** native CTest cases passed, including the previously intermittent legacy live/relay scenarios (parallel local run with per-test timeout). This is Linux evidence only; Windows-native and browser GUI acceptance remain open.
