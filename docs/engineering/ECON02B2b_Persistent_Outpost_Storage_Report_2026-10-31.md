# ECON-02B2b — Persistent constructed outpost storage

Date: 2026-10-31 · local Linux implementation
Base: ECON-02B2a / item-catalogue inventory / `galaxy-kernel-v24` save format.

## Implemented

- Added a durable, **one-per-built-outpost** storage container (`OutpostStorage`) created only by a valid nearby, same-owner colony ship's authorized `ConstructStorage` command. A real construction module is consumed and two ore-equivalent units are embodied in the structure. This is a bounded first outpost gate, **not** player-free placement or generic multiple container entities.
- New `TransferStorageStack` command routes actual named items through one of four authorized paths: ship→storage, storage→ship, workshop→storage and storage→workshop. The workshop's input/finished bins and the ship are queried through the ECON-02B2a authoritative catalogue-backed stack views. All-or-nothing `StackStore::transfer_to` validates available source quantity and destination gameplay-volume capacity before committing quantities back into world state.
- Storage volume is **4,000 litres (4 m³)**; no mass/density/payload limit was introduced. First integration exposes four existing workshop goods: aluminum, panels, frames and scrap (all at 1 litre per unit under the current game catalogue). More goods, capacities, placements and utility equipment require later increments.
- World authorization, actual constructed outpost, ownership, same system, ship position/docking state, and actual installation of the optional workshop finisher are checked by the simulation before a transfer. No remote teleports; no direct unjournaled mutation API.
- Snapshot version **25** adds deterministic, validated `storage|` lines with stable outpost/site identity, founding ship and exact inventory stacks. Existing versions 1–24 remain readable; SQLite schema version remains 17 because existing journal/ledger tables handle the new command codes.
- Every successful construction or transfer produces an independently validated SQLite resource ledger receipt. Recovery cross-checks command ID, physical action, tick, quantity, item-specific source and destination endpoints, and expected event counts; a forged transfer destination causes restore to fail. `total_material_kg()` counts container inventory and embodied construction modules, preventing hidden minting or disappearing resources.
- A full storage destination refuses new goods without consuming the source. Existing production machinery remains constrained by the previous full-input-batch and finished/scrap-bin rules.

## Local verification

- Linux CMake/Ninja Debug build: PASS.
- Full CTest native/integration suite (includes previous live relay and workshop scenarios): **74/74 PASS**.
- Node.js: **84/84 PASS**.
- Python design: **3/3 PASS**.
- New `galaxy_econ02b2b_storage_tests` exercises construction from an actual module, duplicate refusal, wrong-ship denial, finite 4 m³ capacity, ship↔storage and workshop↔storage material transfers, old production with an assigned resident NPC, mixed aluminum/panel/scrap stacks, resource-conservation invariants, v25 snapshot roundtrip, SQLite restart and ledger-endpoint tamper rejection.

## Boundaries and next gates

- The first storage container is associated with the existing outpost *site*, not a freely placeable multi-container spatial entity. Its 4 m³ capacity is a test balancing value, not a universal cap on factory size or future warehouses.
- It supports the four currently modeled workshop item kinds, not the entire material catalogue; expanding to arbitrary catalogue IDs will require a versioned item-code command contract.
- There is no automated conveyor, multi-node routing, throughput/time-of-flight or NPC logistics automation, nor a graphical factory designer. The container is physically represented in authoritative local game state and world accounting, not yet rendered as an in-world mesh.
- Do **not** interpret the 74 native passes as Windows-native acceptance, graphical playthrough or full multiplayer verification.

**Next:** ECON-02B2c: versioned item IDs; multiple named containers and explicit directed, rate-limited transport links with backpressure and deterministic distant/nearby accounting. No factory footprint caps, no tonne-pallet requirement, no payload mass.

Repository policy: GitHub is for source control and verification records, not the development hub. All build and validation were run locally.
