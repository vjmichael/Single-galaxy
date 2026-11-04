# COLONY-AUTO-04D.4 / v71 — Physical Population and Life-Support Custody

**Date:** November 3, 2026. **Branch:** `integration/colony-auto04d4-resource-custody-v71-20261103`. **Base:** v70 AUTO-04D.3.

## Current implemented slice: 04D.4A

### A physically present NPC is one consumer, not two

Previously, each colony ship's aggregate `crew` count consumed life support even when a named native crew member was physically aboard another ship as a passenger. The receiving ship's embarked passengers were not charged. This could both distort the rescue's life-support forecast and double-charge original vessels after passenger needs were added.

New v57 semantics define the shipboard needs population as the number of **living named NPC records whose physical `location` equals that ship**, regardless of original employer or whether they are a native crew member or currently booked passenger. This is an exact physical compartment count; it does not spawn, kill, teleport, replicate or make anyone unaccounted.

Every 20-tick consumption window now removes up to one unit EACH of food, water and oxygen per physically present person from the **actual local ship**. Food becomes organic waste, water becomes wastewater and oxygen becomes CO2, following existing conservative mass-flow rules. An NPC who boards a ferry stops consuming at their home ship and starts consuming aboard the ferry. After real disembarkation, the opposite occurs.

The corresponding `colony_safety::assess` assessment uses the **same physical population** as the consumption kernel. The recipient's rescue admission supply budget counts everyone already physically aboard plus the new incoming passengers. No speculative cargo, unpaid supply deliveries, or off-ship inventory can count.

### Replay-compatible versioning

A behavior change applied silently to old save versions would invalidate historical operation-journal replay. Therefore:

- `Simulation::enable_passenger_life_support()` is a **genesis-only** explicit opt-in that requires the existing validated, named NPC/assignment format (v32+) and no accepted commands or elapsed ticks.
- The snapshot is now `galaxy-kernel-v57`; its existing record schema remains compatible with v53 structures. The canonical encoder and strict decoder recognize v57 explicitly.
- Older v1–v53 snapshots retain their historical crew-based needs accounting and are **not silently migrated**.
- A future dedicated migration must journal the change in authority, recompute life-support runways, reconcile cold/detailed populations and run independent operation-log replay before allowing upgrades to active worlds.

### Tested behavior

- `COLONY_AUTO04D4_ResourceCustodyTests.cpp` creates two real colony ships with two native crew temporarily transported aboard another. The ferry consumes resources for all six people. The origin consumes **none** while both crew are absent and resumes consumption when they disembark. The test checks real physical NPC counts, food/water/oxygen, whole-world mass conservation, database crash rollback and restarted state.
- `COLONY_AUTO04D3_FerryRescueTests.cpp` now exercises v57 needs. Source risk reaches Critical sooner when it contains four additional true passengers. After an applied rescue, four passengers consume onboard the stocked receiving ferry instead of the endangered source. A nearby but depleted 16-crew ferry is refused as receiver; identity and cargo totals persist.

### Fail-closed full-colony evacuation boundaries

This work is a **necessary physical foundation**, NOT the complete promised full-colony evacuation feature. Existing code permits passenger-only ferry transfer, but:

1. A `ColonyShip::crew` field is currently an enduring assigned organization headcount. Zero native crew on an intact ship violates existing snapshot invariants. No verified mothball/abandonment lifecycle exists for evacuating its last crew members.
2. Farm workers and habitat residents have independent, local life-support stocks and persistent home/assigned-site identities. A wholesale transfer needs atomic worker assignment, social ownership, facility vacancy, and detailed/cold cohort reconciliation.
3. Child/dependent travel has separate ART02 guardian, berth and lawful custody records; a generic NPC passenger manifest cannot move dependent households.
4. Receiving rescue vessels must be assessed for physical capacity, supplies, and route; interregion hauling, journey fuel and multi-cycle passenger sustainment are not yet universally escrowed.
5. A failed or partial rescue must never label a vanished site, lost crew or orphaned dependent as 'safe'.
6. The player's authority does not extend to preventing independent life preservation; but autonomous authority still must execute verified, physically possible actions through the canonical writer.

### Next execution gate

**AUTO-04D.4B — all-or-nothing colony departure manifest and stock escrow:** implement a durable, conflict-safe household/crew/worker roster checked against the actual ART02/ECON03 and world inventory, then introduce physically valid last-crew evacuation/mothballing in the canonical `Simulation` command grammar. Pair this with finite onboard provisions, real receiving/operating crew custody and rollback/restart/cold-warm verification before any 'colony evacuated' outcome. No bypass by player preference, and no fabricated rescue.

## Implemented 04D.4B: complete-colony survey, still not departure

A read-only `colony_evacuation::survey` now gathers the **living** named NPCs assigned to a source colony ship, calculates the existing aggregate crew + farm workers + habitat residents, reconciles each named NPC's current physical location, and reports whether those counts agree. It discovers actual intact nearby colony rescue vessels using the native docking range, exact free passenger berths, and food/water/oxygen capacity for two consumption periods after accounting for people already aboard.

This is a capacity/manifest **survey**, not an admission, seat reservation, or legal guardian check. The result exposes `dependent_guardianship_verified=false`, `last_operating_crew_departure_supported=false` and `executable=false` until the corresponding physical/legal atomic adapters exist. A positive `sufficient_local_berths` cannot be mistaken for an evacuated colony.

`COLONY_AUTO04D4_DepartureManifestTests.cpp` exercises a complete four-person named origin, a nearby adequately stocked receiver, an inaccessible distant ferry, a nearby but underfunded receiver, snapshot round-trip and explicit refusal of any success claim.

### Critical correction: the old home ship must not double-consume

For v57 **the entire physical shipboard population** is counted from living named NPCs with `location == ship`, not `assigned_crew + visitors`. This prevents absent native crew boarded onto a ferry from continuing to consume supplies at their original ship. Disembarkation restores those people to the old ship's consumption count. The ship-level life-support `Assessment`, 20-tick needs kernel, and receiving-rescue budget share this definition; older world versions keep their original replay rules.

## CI

Native GitHub workflow: `.github/workflows/colony-auto04d4.yml`. Tests must pass on the final branch head before the 04D.4A gate is considered verified.
