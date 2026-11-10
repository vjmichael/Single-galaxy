# FP3D-COLONY-01 — Colony spawn → departure dock → space-faring

## Player-facing contract

A new player should understand **where they live, where they leave, and
what boarding means** without learning developer controls. The colony is
the on-foot civil environment. The **dock** is the intentional gateway
from colony life to an existing, owned spacecraft and the wider galaxy.
Boarding is a real transaction across that boundary, not a decorative
teleport or a collision with a white cube.

First-person flow:

1. **COLONY ARRIVALS.** Enter the original station world as a person.
   Habitation modules, service frontage, gray paving, an arrival marker
   and a sign establish that this is the colony, not a cockpit.
2. **DEPARTURE ROUTE.** A visible cyan pedestrian path leads from the
   authoritative player spawn to a **DOCK CHECKPOINT**. At a distance
   the HUD says `COLONY → DOCK`; it does not claim E will work.
3. **DOCK / SHIP BERTH.** A dark-gray landing deck, amber edge guides,
   illuminated boarding ramp, a visible white ship with nose, canopy
   and landing gear, and `DEPARTURES / TO SPACE` wayfinding establish
   the transition. The dock is not an invisible physics trigger.
4. **BOARD.** Within the original host's accepted station-person hatch
   square AND with a real eligible owned docked ship, the bottom
   context panel says **`Press E to Board`**. Pressing E sends one
   host-paced request, never a client-side teleport. Only the C++/SQLite
   original BOARD receipt allows the station→cockpit handoff.
5. **SPACE-FARING.** The original saved ship's cockpit opens
   automatically after verified boarding. Sitting and undocking still
   obey original ship authority, and flight instruments remain separate.
   On return, native docking and disembark reenter the colony dock.

## Coordinate and persistence invariants

| Location | World coordinates | Authority |
| --- | --- | --- |
| Colony PERSON spawn | x=1.6 m, z=0 m | Original C++ PERSON; historic saved location unchanged |
| Dock boarding checkpoint | x=0, z=0 m | Original C++ `StationPlayerAction::Board`, ±600 mm each axis |
| Refinery services | x=3 m, z=0 m | Original C++ terminal, 1200 mm access radius |
| White landed ship berth | x=0, z≈−4.9 m | Godot visual projection of existing owned docked ShipID |
| Pedestrian ramp | z≈−1.9 m | Visual-only; cannot authorize movement or boarding |

No migration, forced movement, new player, free replacement ship or
replacement SQLite profile. Existing saves load in place. The colony
scenery is built from Godot primitive meshes under separate
`ColonyArrival`, `ColonyToDockWalkway` and `SpacefaringDock`
visual nodes. These nodes contain **no CollisionObject3D**: they do not
invent native blockers. Original C++ fences and obstacles continue to
control on-foot movement, original ship custody controls E, and SGDX
supervision controls the single durable writer.

The physical ship hull and native cockpit are retained as the actual
existing prototype objects, repositioned at the dock instead of eleven
metres down a featureless hall. The white ship is visually distinct
from gray station structures; dark glazing, doors, wing surfaces and
landing gear are presentation only. This is still a **graybox**,
not a final colony art/biome or physics-equivalent environment.

## Acceptance and evidence

- Source contract: C++ PERSON genesis and hatch coordinates unchanged,
  no new local boarding authority, exact `Press E to Board` prompt,
  separate owned white ship/dark dock, visual-only scenery.
- Real Godot 4.7.2 `colony_dock_smoke.gd` on Linux and Windows:
  three identifiable regions, new meshes and wayfinding, no new
  collision objects, correctly mapped original player, white ship
  and berth placement, invalid ship denied, E prompt admitted at
  actual original hatch, and station→ship view after *synthetic*
  BOARD frame. Synthetic smoke alone is **not native transaction proof**.
- Existing real-Godot + original C++/SGDX/SQLite integration must still
  pass auto-connect, native walking, E BOARD, cockpit writer handoff,
  single-writer restart, real collision and docking checks.
- Windows ZIP must pass official bundled engine PE/hash/PowerShell
  5.1 clean-launch/tamper rejection and four non-headless frames.
- Physical Radeon AI Pro R9700 remains the independent owner gate:
  from colony spawn follow signs to dock, see **Press E to Board**,
  board without H/P, pilot, undock, contact asteroid, dock, and
  reopen the unchanged saved world.

**Current shipping status:** PR #125 stays **DRAFT** while the
redesign's one exact-head consolidated native acceptance runs and
the creator has not yet completed the full physical-PC loop. The
previous Windows `SGDX supervisor configuration failure: invalid
UTF-8 argument` must be independently rechecked, not assumed
repaired by scene changes. Manufactured mining equipment remains
deferred until physical 3D acceptance.
