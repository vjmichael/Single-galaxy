# ECON-02B2d3a — Durable site-local placement of already-constructed factory equipment

Date: 2026-10-31 | Parent: ECON-02B2d2 (`b8c6ce6346bd05a7d69c9cfdabec605f512c84dd`)

## Implemented

The existing constructed outpost's original storage bin (node 0), its additional module-built storage nodes, and its installed Tier 1 workshop processor may now receive independently recorded site-local grid positions. New `Action::PlaceFactoryEquipment` is a normal authenticated, owner/ship/site-local command and is strictly one-time per installed device. Set `Command.target` to the actual constructed outpost; `amount=-1` installs the existing workshop's position, `amount=0` installs the existing base bin's position, and a positive amount identifies a preconstructed additional bin. `thrust_x/thrust_y` are integer grid coordinates, not force/motion. The positive-node limit remains the existing bounded ID range. Site coordinates are bounded to +/-1,000,000 solely to avoid invalid arithmetic; there is NO factory building-size or machine-count cap.

The code reuses the ECON-02B2d1 `FactoryLayout` collision validator on every proposed placement. The workshop occupies a Tier-1 2×2 footprint and storage occupies one tile in this first vertical slice. Invalid/overlapping placements fail without mutating the authoritative world. The command never creates a node or machine, never grants inventory, and never consumes a *second* construction module: the earlier verified equipment construction spent the material. This is an **equipment installation/position binding**, not construction from a ghost layout.

Snapshot v29 adds `factory_placement|site|device_node|x|y` records for this verified subset. The parser rejects orphaned nodes, orphaned workshop positions, invalid ranges, duplicates, and collisions. Snapshots v1–v28 remain readable and do not gain phantom placed equipment. A new versioned command is added at the end of the Action enum so prior codes remain stable. `Persistence.cpp` command decode accepts the new action, and the existing atomic historical command/event journal secures its state alongside normal world snapshots. Placement makes no resource-ledger entry because no resource transfers on this action; creating the physical equipment remains separately material-backed, logged and immutable.

## Local acceptance

- Linux C++ Debug configuration and all-target build succeeded.
- Dedicated `galaxy_econ02b2d3a_installed_placement_tests` passed, covering unbuilt equipment rejection, non-owner rejection, tile collisions, multiple built storage positions, actual workshop footprint, duplicate placement rejection, no extra modules consumed, total material conservation, canonical v29 roundtrip, SQLite save/restart and failed checkpoint rollback, corrupted layout rejection, and deterministic tick parity.
- Native non-live regression (excluding legacy live/relay tests): **56/56 passed**.
- JavaScript: **84/84 passed**. Python design: **3/3 passed**.
- Full parallel CTest run reached 79/80 passing before interruption/stall in a legacy live/relay scenario. **Not** a complete full-suite pass; Windows-native and graphical gameplay still unverified.

## Remaining ECON-02B2d gates

1. Add durable **path segments, ports, turns, rotation** and validation that a route's installed endpoints and physical path match the constructed link. Keep existing logical rate/filter transfer and inventories authoritative.
2. Make ghost blueprints into authorized construction work orders; charge material once when real construction occurs; support cancellation and staged completion without conjuring or duplicating equipment. Explicitly support authorized relocation or deconstruction later.
3. Add player GUI placement/conveyor tooling; it must issue authoritative commands rather than editing snapshot state.
4. Generalize physical installed machine IDs (including Tier 2–5 and finisher) and player-configurable machine/utility types; avoid presized factory rooms.
5. Verify remote aggregated simulation parity with dynamic routing, deliveries, maintenance, NPCs, and event checkpoints. Windows-native and full graphical play are open gates.

GitHub is the source and milestone repository, not the main development environment.
