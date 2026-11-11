# Dock District, Colony Inventory, Population LOD and On-Foot Movement

Engineering design and implementation register — 2026-11-11. **Development branch, not a verified release**. No routine CI, draft PR, native acceptance or packaging submissions are authorized during this building phase.

## Player-approved governing design

The dock is the interface between a physically situated colony and space. It is a **separate transport/logistics district** with a protected public boarding path, freight staging, and a named-worker dispatch area. The colony can have a population in the millions, but rendering and pathfinding every resident is forbidden. Its original simulation may aggregate people; only a bounded small fraction appears in Godot, and that display must never invent inventory, job outcomes or actual NPC identity.

Inventory has **EVE-like location custody**: one logical, private inventory for each owner at each colony/station (the native `(station EntityId, PlayerId)` hangar plus native owned station facilities). An item left at Colony A cannot be accessed at Colony B; it must be loaded into a physical ship and transported. Do not create an account-global stash or copy inventory into UI memory as a second authority.

The player's docked-ship ore is transferred by native `UnloadOre` to the exact owner's **colony station hangar**, not the avatar's pocket. Refining at Colony A works from the same colony interface and requires an actual original-world constructed, crewed, powered facility, with source lot, finite inputs and original SQLite commit receipt. The interface is permitted anywhere **inside that original colony**, so players do not have to walk to the actual refinery or factory for control. Physical construction, material transfer, equipment, NPC assignments and logistics remain real; only the operator's local console access is virtual.

Manufacturing/ship-module design is **deferred**; no artificial recipe or module provisioning may be used to fill the gap. The eventual manufacturing panel must issue original approved industry/workshop commands and deposit legitimate output at the **same colony**, not at a remote station or on the person.

On foot, pressing **Shift** toggles running ON/OFF; holding Shift is not required. Flight remains the original supervised third-person cockpit with separate inputs.

## Implemented source paths

- `desktop_godot/world/colony_dock_layout.gd`: modular swappable district, preserving native (0,0) hatch, real ship ID and old saved-world positions. Central passenger route, separate outer freight corridors, physically distinct staff dispatch and owner-specific staging area. All visible meshes are noncolliding and do **not** create a working refinery, new NPCs, or arbitrary item stock. Industrial detail is a temporary construction baseline; creator will revise art design later.
- `include/galaxy/StationRefineryProtocol.hpp`: original physical-terminal access still available, plus **same-colony resident** permission for explicit colony-console frames. The old owner/station/lot/ship validation remains; no account-global read or remote ship operation.
- `src/desktop_station_host.cpp`: `COLVIEW`, `COLUNLOAD`, `COLREFINE` map onto the **original** one local hangar and native refinery, preserving one-use request, source generation, actual docking, immutable journal receipt, original staff/power/site and source mass. Distant station IDs are denied. No additional persistent inventory ledger.
- `desktop_godot/world/native_station_session.gd`: calls the existing native host for those three operations, validates standard `REFHEAD/REFLOT/REFMIN/REFEND` as source of truth.
- `desktop_godot/world/refinery_terminal_ui.gd`: visible local **colony inventory** and operations panel; mineral output always shown as stock at its original station ID, not as personal goods. A future manufacturing section is explicitly disabled.
- `desktop_godot/world/main.gd`: **I** opens the colony interface anywhere inside the player's original colony; R remains an alias. Both are unavailable while aboard a ship or disconnected from that exact station.
- `desktop_godot/world/player_controller.gd` and `native_station_session.gd`: Shift toggles walking/running. Native speed is bounded through request cadence (walking ~120ms, running ~70ms, same legal 100mm original C++ step; host retains its own 45ms minimum), not speculative player motion.
- `src/desktop_station_host.cpp` read-only `POPVIEW` and Godot `dock_population_lod.gd`: at most 4,096 original NPC records examined per requested observation, actual station identity/tick/generation required, truncated result explicit; **24 civilian and 8 named dock-worker 3D proxies maximum** independent of source population scale. Those graphics create no NPC custody/AI/physics/ship crowding.

## Open work — do not falsely mark complete

1. **Dedicated dock-worker assignment authority:** the dock has a reserved independent staff district and a read-only view of existing actual named station-located workers. There is not yet an owner-authorized, persistent `AssignDockWorkers` command, skill/schedule/shift system, staff reservation, or worker-dependent unload throughput; do not treat a generic ship crewman or decorative capsule as employed dock labor.
2. **Passenger/freight capacity scheduling:** implement original-world bounded berth queues, staff-dependent cargo loading throughput, passenger processing, priority arbitration, sustained simulation of millions by aggregate cohorts, durable interrupted jobs, and worker shortage backpressure. Never spawn one graphical actor per simulated resident.
3. **General unified production goods:** currently one logical player colony panel projects the **real hangar ore plus owned refinery mineral output**. Construction/workshop output storage and movement into the same colony inventory need original industrial/warehouse custody integration without inventing an account-global balance.
4. **Full remote factory commands:** can be selected from this colony's panel once original infrastructure, approved recipes, workers, power, finite stock and ownership are implemented. No remote-universe material access.
5. **Creator-led visual redesign:** art is modular and replaceable. No claim that the present staging meshes or decor constitute the final colony architecture or detailed station.
6. **Consolidated source verification only after development backlog:** current changes intentionally have no new CI/PR acceptance claims. Preserve original installed world and do not block future functional engineering on repeated GH status checks.

Acceptance, when scheduled in a consolidated batch, must prove two physically separate colonies with isolated inventories, owner and locality checks, physical unload/refinery conservation, authorized service from public concourse but rejection from another colony, one-use request and restart correctness, bounded visual NPC count at very large population, and Shift-toggle walk/run after native station restart.