# S6-F5 — Native Original Planetary Surface Movement

**2026-11-12. Branch:** `sg/stage6f5-native-surface-movement-20261112`, stacked on S6-F4 `4b645ebb`. **Status:** F5-A landing-pad native player/named-NPC movement **SOURCE IMPLEMENTED provisionally**; native compilation, CTest, actual host/UI acceptance, installed Windows/Linux and physical Radeon AI Pro R9700 **NOT RUN**. No PR, merge or release authorized.

## Core rule

The ONLY canonical people are `Simulation::player_people_` and `Simulation::npcs_`; the ONLY physical location is the previously constructed original `PlanetarySiteFacility`. The `PlayerPerson` must have the explicit v131 arrival/descent/disembark proof; a named NPC must already occupy the exact original surface facility. World location and contact come from `Simulation::scan_planetary_geography`, not a Godot scene, invented planet inventory, client height, arbitrary XYZ destination, synthetic NPC or a new movement-worker authority.

## Implemented source changes

**F5-A1 — Native surface coordinates/canonical v132.** `NativePlanetarySurfacePose` is appended to the existing `PlayerPerson` and `NpcRecord` (x/y/z mm, last tick, one-use command receipt). The original canonical `galaxy-kernel-v132` handles both, accepts v131 and older snapshots unchanged, rejects invalid coordinates or forged/missing movement receipts and persists the canonical `surfacemovereceipt` one-use ledger. The existing SQLite `PersistentWorld::Store::commit` persists source identity, pose, receipt, world generation and whole-world state atomically; failpoint rollback does not mutate the live world.

**F5-A2 — Bounded original source terrain contact.** `PlanetarySurfaceMovement.hpp` adds a restricted **32-metre-radius-class landing-site domain** (±32m per axis), a **300 mm per committed tick** human step or **250 mm** named-NPC step, and exact fixed-point ground contact derived from the ORIGINAL watershed region's existing survey elevation (1 survey elevation score maps to 1,000 mm of *local gameplay contact height*; it is NOT scientifically calibrated planetary altitude). There are no client-supplied heights, physics impulses, Godot geometry authority or source resource changes. Water-covered original cells reject walking; built original housing/utility phases reserve conservative, marked local equipment-pad collision rectangles. Known on-foot named actors have 0.5m separation. Unpositioned legacy NPCs receive no fabricated world coordinates.

**F5-A3 — Original environment and gravity contact.** Existing persisted `PlanetaryPhysicalEnvironment` and `PlanetarySiteFacility::ecology_last_hazard` gate cold/heat/flood movement. In newly dangerous conditions only movement *closer to the original hatch* remains allowed, preventing an unintended permanent inability to retreat. Original ground height is settled by the trusted source on each accepted footstep; no unverified client free-fall/jump/gravity impulse, oxygen or damage is inferred. Full atmosphere/suit, terrain slope and vertical dynamics remain open.

**F5-A4 — Player/NPC transport and return.** Only the owner-approved v131 `AtPlanetarySurface` player can walk; other owners, unregistered people, shuttle passengers, flight-in-transit requests and cross-site or cross-body requests are denied. An existing named NPC can walk only if its ORIGINAL physical `NpcRecord.location` equals the actual owner-controlled site and its role is a shipboard or planetary-surface worker. `Reboard` requires player proximity **within 1.5m** of the existing hatch. Original pilot/passengers who have walked also must be near hatch before ascent. Their active ground poses are cleared upon actual ascent; visitors clear their surface pose after a real shuttle reboard. Planetary motion cannot authorize cargo/immigration or the parent's departure.

**F5-A5 — Original owner native host/packet, not visual control.** `colony_live_host.cpp` accepts authenticated `PLANET_SURFACE_MOVE` and `PLANET_SURFACE_STATUS` for PLAYER or an existing NPC. The client supplies only bounded signed delta and once-used request ID, never a position, height or collision outcome. The host reports exact original ground x/y/z, last move tick, receipt, source region and world generation after commit. `Stage6GodotPacket.hpp` carries read-only 64-bit string native pose/receipt if genuinely landed, and `godot/stage6` displays that evidence without moving or resetting its **separate local inspector camera**. No installed-game input controls have been wired.

**F5-A6 — Existing browser relay / owner UI source.** `live/interactive_relay.py` now strictly validates `colonyPlanetPlayerRegister`, `colonyPlanetPlayer` (STATUS/BOARD/DISEMBARK/REBOARD/EXIT), and `colonyPlanetSurface` (STATUS/MOVE) with exact field sets, physically valid original body/cell numeric bounds, actor type, valid existing NPC ID, bounded signed mm deltas, and fresh sub-`2^50` request IDs. `viewer/colony.js` has non-CLI owner controls in the existing per-planet shuttle panel and strictly decodes original player/ground receipts, signed original XYZ, world generation and denial. It has **no client-authoritative pose or collision predictor**. `tests/STAGE6F5_SurfacePacketContractTests.cjs` is registered as an optional Node CTest; source is staged and the test is **not executed**.

### Native host commands

```text
PLANET_SURFACE_MOVE|BODY|LAT|LON|PLAYER|DX_MM|DZ_MM|RECEIPT
PLANET_SURFACE_MOVE|BODY|LAT|LON|NPC|EXISTING_NPC_ID|DX_MM|DZ_MM|RECEIPT
PLANET_SURFACE_STATUS|BODY|LAT|LON|PLAYER
PLANET_SURFACE_STATUS|BODY|LAT|LON|NPC|EXISTING_NPC_ID
```

Owner and ship come ONLY from the existing authenticated native host principal. Coordinates remain local to the exact original physical `PlanetarySiteKey`; crossing another 5° survey cell by manipulating client input is not accepted. Each actor can commit at most one bounded step per native world tick; new steps require the original world stepping.

### Source fixture (REGISTERED, NOT EXECUTED)

`tests/STAGE6_NativeSurfaceMovementTests.cpp` registered as `galaxy_stage6_native_surface_movement` reuses the original mined/fabricated/fueled/constructed site and actual named-pilot shuttle voyage to stage:

1. Wrong owner, nonexistent NPC, premature surface move, in-flight move, excessive/diagonal step and forged reuse refusal.
2. Original human disembark, recorded 300mm movement with exact surveyed ground height and native Godot packet readback.
3. `FailPoint::AfterSnapshotWrite` no generation/hash advance and safe retry of the same request; atomic restart from a copy of the existing original SQLite save with exact same pose/receipt.
4. One-step-per-original-tick refusal; hatch-distance reboard refusal; return via committed reverse steps; no remote original vessel ascent while player is ashore.
5. Original named pilot moves beyond the 1.5m hatch, denies native ascent, retains the posture through independent SQLite recovery, walks back and then ascends.
6. Source-level cold/flood contact, original-water and conservative equipment-pad collision primitives; original material/source conservation and canonical v132 deserialize.

**The C++ fixture and the optional Node strict-decoder fixture are authored assertions, NOT test results.** There is no compiled exact-head proof, full host/relay handshake test, genuine physical Radeon gameplay observation or signed installed app acceptance.

## Remaining F5 work / blocker to calling whole-planet walking complete

- **F5-B continuous geographic navigation:** the existing coarse 36×72 watershed survey cannot prove micro-terrain slopes, detailed collider footprints, traversal between cells, or the true contact surface of the 3D visual mesh. Design a physically consistent finer original-surface representation and immutable geometry identity; include polar/cell seams and return-to-base route.
- **F5-C vertical physical simulation:** true gravity vector and accelerations, jumps/falls, climbing, elevation interpolation, surface tools, environmental protection, oxygen/heat injuries and native collision against actual generated buildings/vehicles are not implemented by this initial height-snapped pad.
- **F5-D actual autonomous NPC pathing:** the existing named NPCs can execute authenticated finite steps, but independent real NPC scheduling, navigation tasks, station-to-surface crew transfer and high-population performance are open.
- **F5-E native installed control:** the **existing browser colony viewer** now has strict relay/owner controls, but connect them to the actual installed `desktop_godot` playable person and native collision acknowledgements, persistence/replay on disconnect, native-to-Godot frame conversion and original visual interpolation; prove human UI denial/refusal/cancellation.
- **F5-F source repair/consolidated acceptance:** exact-head C++17 Linux/Windows compilation, all affected CTest/old SQLite corpus, negative tamper and speed/race fixtures, multi-cell long-run simulation, physical Radeon installed input and rendering; no CI/PR acceptance gate is claimed closed.
- **S6-F6** planetary approach, continuous bounded LOD paging, cap/seam, native renderer-worker and large-population scalability; **S6-G** consolidated source/runtime/installed release gates.
- **S6-D5** cross-region geology/water/sediment transfer, onward soil/mineral/volatile freight; **S6-E** extended ecology and actual native industry acceptance stay OPEN.

**Next engineering group:** S6-F5-B continuous native physical terrain, navigation and collision generation, not a shortcut that treats Godot preview triangles as the server-authoritative world.
