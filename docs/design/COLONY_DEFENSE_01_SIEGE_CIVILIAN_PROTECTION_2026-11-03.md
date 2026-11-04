# COLONY-DEFENSE-01 — Colony and Civilian Defense Architecture

**Design gate** · v80 discovery, November 3, 2026. **Implementation status:** NOT YET a working combat or siege system. Do not infer host combat capabilities from this document.

## Why this must be designed before player-versus-colony attacks

Single-Galaxy is not an empty space shooter: it has actual persistent colony inventories, finite named staff, dependent children, family and guardian custody, and warm/cold population authorities. A hostile player can eventually attack stations, habitats, farms, utility systems, supply convoys, infrastructure and people. This is part of the game world, but no action should turn a colony into a silently deleted row, instantly annihilate a population merely because the client clicked the station, or generate free replacement personnel/defensive ammunition.

Currently `Simulation::HostDestroyShip` is a trusted/test-only destructive event and **explicitly refuses colony ships**. `HostCrewLoss` is also host-only, not a gameplay damage model. They must NOT be exposed directly as siege or colonist targeting gameplay APIs. Civilian protection is implemented by physically bounded defenses, priorities and real evacuation, not by an arbitrary blanket invulnerability switch.

## Engagement model: detect, identify, decide, respond

**Threat detection.** An NPC defense coordinator only detects ships within the real sensor/LOS/scan volume of staffed and powered detection equipment, a vessel actually on watch, or an already-authenticated incoming impact. No database-wide omniscience. Detected contact is a local observation with source, time, uncertainty, and provenance. The GRSM rumor/social system may later spread it but does not automatically upgrade it to verified fact.

**Threat classification.** Unknown ship ≠ hostile. Distinguish transiting, suspicious approach, weapons lock, trespass after warning, confirmed attack, and ongoing damage. Player faction and reputation may influence alertness but may not assert an attack that did not happen. Whether private colony rules permit hostile trespass should be visible and enforceable through a defined rules-of-engagement policy.

**Defensive staffing.** Security NPCs are drawn from the finite named workforce; they cannot simultaneously fill a medical, utility, farming or transport slot. Duty and availability checks apply, with fatigue/injury/casualty consequences. Player can staff watch posts, assign patrols and set permitted engagement conditions. Alerts and evacuation can recruit existing staff only through explicit reassignment and persistent job records.

**Defense hardware and resources.** Early physical defenses should include hull bulkheads, sealed compartments, powered warning sensors, hardened shelters, barriers, batteries and turret emplacements. Weapons require actual built mounts, energy or manufactured ammunition, magazine/storage capacity, maintenance, firing arcs, LOS, targeting confidence and cooldown. Armor and module damage consume physical parts during repair; wrecks preserve salvageable leftovers. The same power grid/utility system that supports civilian production should limit sensors and turrets under brownouts.

**Damage, not teleportation.** Offensive combat first creates an authenticated **impact record** tied to a physical source entity, world tick, projectile/hit trace and target component. Apply deterministic, bounded shield/armor/hull or site-module damage only under a single authoritative world step. Distinguish disabled utilities, damaged oxygen systems, decompression, injured residents and destroyed facilities. Do not automatically kill children because a building has 0 condition; casualties require explicit physically supported cause and durable ART02+population consequences.

**Shelter and evacuation.** Threat and life-support severity are independent signals. NPC leadership should prioritize shelter in reachable, stocked compartments, seal off damaged sections, dispatch available medics and repair crews, request backup, and start **the existing indivisible v76-v79 family evacuation** where reachable rescue ships and the owner/cold-region authority permit it. A combat evacuation must conserve guardians, people and their region custody; if safe local evacuation is impossible, the system records distress and temporary shelter rather than duplicating families across regions.

**Gameplay fairness.** Attackers can blockade and damage a base given actual capability and time. Defenders get visible warning opportunities and a response window proportional to sensor and attacker capacity; no magical invulnerability, guaranteed interception or arbitrarily doomed NPCs. The protected core of the early game may use stronger public enforcement than frontier space, but that is world enforcement with ships/staff/resources, not a universal rule making attacks impossible. Insurance/free NPC replacements/PLEX or RMT hooks are out of scope.

## Engineering sequence

- **DEF-01A — Real threat/contact evidence.** Add read-only, position/sensor-range bounded ship observations, authenticated hostility escalation, immutable alert records, and restart verification. Make tests with neutral flybys, stealth/missing sensors, false warnings, real attackers and multiple colonies. Distress cannot identify unseen attackers.
- **DEF-01B — Physical static defense and staffing.** Build player-constructed sensors, shelters, hull reinforcement, turret mounts, magazine/energy inventory, NPC duty scheduling and real maintenance. Expose damageable entity IDs and component condition in the canonical snapshot.
- **DEF-01C — Authoritative attack and damage pipeline.** Validate a real attacker, position, weapons/energy/ammunition and cooldown, targeting/LOS, hit and damage. One world transaction updates target module, inventory, threat evidence and activity events. Test restart, race/replay, friendly-fire and ownership changes.
- **DEF-01D — NPC combat response policy.** Evaluate observed hostilities, assign staff within the live workforce, route shelter/repair/resupply/defense as bounded competing work orders, and surface alerts to the player without erasing established casualty provenance.
- **DEF-01E — Civilian evacuation and recovery.** Reuse the canonical household transaction for rescue; account for wounded/deceased through separate cause-backed records and ART02+ECON03. Require proof of receiving ship capacity, regional ownership, no duplicate population, persistent ruins and partial cargo salvage.
- **DEF-01F — Siege acceptance and economic costs.** Simulate long blockade, destroyed resupply convoy, turret ammo depletion, habitat leaks, defender retreat, authority corruption and save/restart. Quantify attacker/defender resource conservation and CPU cost, not just whether any animation plays.

## First playable minimum

For an early playable vertical slice, one player-owned outpost should have a visible defense status pane: detected contacts, confirmed incoming attacks, named available security staff, actual shield/armor/structure condition, power/ammo stock, reachable shelter, relief/repair readiness and a single player-set rules-of-engagement policy. None of these should report a physically successful action before a canonical world receipt exists.

**Do not combine this with the unfinished 04D.5C local supply gate and claim that siege/weapon combat is already implemented.** This document establishes the required constraints and prioritized implementation phases.
