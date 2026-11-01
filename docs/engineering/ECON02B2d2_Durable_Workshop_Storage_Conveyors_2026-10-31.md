# ECON-02B2d2 — Durable storage-to-workshop and workshop-to-storage logistics

Date: 2026-10-31 | Parent: ECON-02B2d1 `5a5c045b53cc79ba526ab9970cfd8c3a2c79d909`

**Status:** Implemented in the native C++ local simulation and SQLite world journal. The module-backed connections are **logical installed links**; their geometry is not yet assigned to an ECON-02B2d1 spatial blueprint, and there is no interactive conveyor placement GUI.

## Scope and game behavior

- Added `Action::ConstructWorkshopLink`, an authenticated, owning-ship, physically local command accepted only at an already constructed outpost containing real storage and an installed workshop. Each accepted command consumes one existing construction module (2 ore-equivalent tonnes permanently embodied in the link), and adds a durable route identified by the command ID.
- `Command.target` is the constructed outpost; `thrust_x` identifies an existing site-local storage node (0 = base container), `thrust_y` selects 0 = storage-to-workshop feed or 1 = workshop-to-storage output, and `amount = 4 * item-units-per-tick + item_index`. Permitted units/tick: 1..1000. Item index maps 0 aluminum, 1 panels, 2 frames, 3 scrap. **Only aluminum feeds the first workshop machine**. Only panels, frames (if second machine installed), and scrap can leave the workshop. Reverse same-item recirculation through a workshop port is thus impossible under this first scope.
- Workshop route endpoint uses the reserved `kWorkshopConveyorNode` ID; ordinary storage nodes retain their previous IDs, filters, rates, and volume capacities. The transport routine now transfers directly against the existing `OutpostWorkshop` fields (`aluminum_kg`, `panels_kg`, `frames_kg`, `scrap_kg`) rather than introducing any duplicate factory inventory. Input stock remains in the real warehouse until transfer; output cannot move without actual completed manufacture.
- The original quantified production kernel still requires complete 20-unit input batches, physically present authorized NPC operator, working power and condition, and available finished/scrap capacity. **Below-minimum inputs result in zero output, zero scrap, zero partial consumption.** Existing reserved WIP remains durable through interruption and restart.
- An empty source or full destination applies automatic backpressure. A storage-to-workshop link can be created before a shipment arrives, but cannot produce material on its own. Actual transport uses per-item gameplay volume (m³) and bounded per-tick quantity, with no payload weight mechanic.
- The older `ConstructStorageLink` endpoint validation was hardened to reject values outside its real site-local storage-ID range before narrowing to `uint32_t`.

## Persistence and authentication

- Snapshot **v28** adds no new SQLite schema tables; it extends existing durable `storage_link` records with a reserved typed endpoint and the append-only action code. Snapshot restore validates endpoint type, actual installed workshop and source ship, allowed item direction, finisher requirement, rate, all construction receipts, transfer event totals, and storage-only cyclic paths. Prior snapshot versions v1–v27 remain readable.
- Construction command results continue to have an executed resource ledger receipt (`storage_link_construction_t`), and automatic workshop transfers receive the same atomic per-tick `AutoStorageTransfer` activity with a **distinct `workshop:<site>:<item>` endpoint label**. The matching SQLite receipt is committed with the authoritative world checkpoint. A forged workshop-side destination/source is rejected on recovery.
- A simulated interruption after writing an uncommitted checkpoint rolls back the entire tick; no half-moved material survives.

## Local acceptance evidence

- Linux Debug build, native standalone `galaxy_econ02b2d2_workshop_links_tests` passed.
- Full **non-live native** suite: **55/55 passed**; Node tests **84/84**; Python design tests **3/3**.
- The full concurrently scheduled native suite was attempted twice but stalled in separate legacy live/relay scenarios. The independently rerun legacy `galaxy_b64_live_multihop_e2e` passed 1/1. Do **not** describe the incomplete full suite as 79/79 green.
- New test covers actual construction spending, ownership, wrong node, incompatible item direction, no unbuilt finisher output, empty-stock no-op, real aluminum transfer, operator-driven manufacturing, finished panels and scrap retrieved through individual automatic links, total physical inventory conservation, deterministic step parity, v28 canonical snapshot roundtrip, SQLite restart/resume, checkpoint-write failure rollback, and forged factory-side ledger rejection.

## Remaining ECON-02B2d gates

1. **Bind authored geometry to installed objects.** The B2d1 `FactoryLayout` validator knows spatial rectangles and routing paths; B2d2's world command knows installed container and workshop IDs. Persist/authorize their association atomically with actual construction receipts, including placement position and player layout ownership. No unchecked client blueprint may create an object or move physical inventory.
2. **Freely place and rotate equipment, conveyor segments and utility devices** with a GUI/blueprint construction mode and NPC-build work orders; maintain no artificial factory floor limit.
3. **Merge/branch/filters, transport maintenance and power costs, collision/elevation rules**, and proper `m³` volume throughput constraints in the real world.
4. **Real remote simulation scheduler:** repeated in-world ticks remain the authoritative baseline; no galaxy-scale optimized multi-route simulation or event-log compaction is claimed. Off-screen parity and maintenance/resource arrivals require further tests.
5. **Windows native and independent graphical play acceptance** remain open.

GitHub is only the source repository and milestone record. Development/testing happened locally.
