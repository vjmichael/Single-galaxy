# PLAY-08 — Native Training Combat and Persistent Wrecks
**November 4, 2026 · Branch: `integration/play08-ship-combat-20261104`**

## Scope and game design
PLAY-08 establishes a real, deliberately limited **player-fired ship-weapon vertical slice** in the existing Single Galaxy C++/SQLite world. It does **not** introduce a parallel combat simulation, character XP, NPC crew battles, general PvP, colony destruction, or new starter infrastructure.

The native colony genesis provisions one *uncrewed practice drone*, approximately 8 km from the player-controlled colony ship. The player has 12 finite starter weapon charges and 100 hull; the drone has 45 hull and no ammunition. The drone is not a human crew member, colony population, station, or aggressive attacker.

The native `FireTrainingWeapon` command is appended to the Action enum (preserving historical IDs); requests flow through the authenticated relay and native per-turn player ownership check. Every shot must satisfy:
- the firing ship is owned by the issuer, in open space and not jumping;
- the target is a real living, explicitly flagged uncrewed training drone in the **same star system**;
- ship-to-target distance is at most 12,000 mm;
- the target lies in a native-computed 90° forward cone of the ship's persisted yaw (45° to either side);
- at least one real weapon charge remains;
- at least two committed ticks have elapsed since the previous successful shot.

On a successful shot, the native host consumes exactly one charge and reduces drone hull by at most 15. The command's result reports actual damage. Zero hull converts the entity to the existing persistent **Wreck** type, retaining its location, ownership and physical world identity. The wreck cannot be fired on again. No salvage claim or loot generation is fabricated.

## Persistence and invariants
Combat attributes are versioned in **canonical snapshot v62** (per-entity hull, remaining charges, last successful fire tick, training-drone identity). Old snapshots v61 and earlier remain readable; historically persisted ships acquire default starter combat values on load, which are **not serialized until a v62 game action or genesis training-drone provisioning upgrades the world**. Invalid v62 records (negative hull/charges, future cooldown, weapon-bearing nonships, unauthorized training drone) are rejected. Existing vessel-loss-to-wreck transitions clear active weapon state so a ferry casualty cannot become an impossible armed wreck.

The engine remains the single world writer; the browser cannot set hull, ammunition, outcome, position or target type. A transient HTTP 202/queue notification never counts as a hit.

## GUI
The optional combat panel displays authoritative ship hull and weapon charges, target drone IDs and hull, and a Fire training weapon action. The combat target must originate from native `COMBAT_TARGET` telemetry and the real `SGVIEW` entity feed. The UI reports wreck status when the native projection changes the drone entity to a wreck. The first-outpost tutorial remains skippable. Existing mining, travel, camera, outpost cargo, yaw and persistent game systems are unchanged.

## Verification
- `tests/PLAY08_TrainingCombatTests.cpp`: actual C++ journal/SQLite shot authorization, charge consumption, cooldown refusal, hull damage, wreck transition, invalid subsequent fire, reload and recovery copy.
- `tests/PLAY08_CombatWiringTests.js`: strict combat telemetry parser, authenticated relay command, GUI and no client-world-write regression.
- `.github/workflows/play08.yml`: builds the real native kernel/host and runs both tests plus previous PLAY-02–PLAY-07 browser checks.

**Acceptance pending:** exact-current-head GitHub CI result, live local-HTTP→native→SSE firing end-to-end, Windows Chromium usability and controls, and performance of rendered combat. The stationary practice drone does not pursue or retaliate; general AI enemies, shields/armor balancing, weapon crafting/reloading, salvaging, tactics, NPC loss, and multiplayer combat are intentionally deferred to subsequent milestones rather than implied complete.
