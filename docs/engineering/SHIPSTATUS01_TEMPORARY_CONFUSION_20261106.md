# SHIP-STATUS-01 — Temporary Ship Status Effects / Confusion

**Status (2026-11-06):** first native policy kernel and PLAY-11 fitting adapter committed on a new stacked draft. CMake + Windows/Linux regression source submitted, exact-head CI/native outcomes not verified. **No actual live player/UI switch or production Simulation action gate is implemented in this increment.**

## Decision
EVE-style energy neutralizers and energy siphons are explicitly **not** part of Single-Galaxy. Separate:
- **ECM:** temporarily degrades ship electronics or hardware capability.
- **Hacking:** compromises pilot information and computerized module-control behavior.
- **SHIP-STATUS:** one mechanism for temporary, bounded effects; not a parallel ship system.

**Confusion** randomly toggles a bounded number of modules between effective ON and OFF. These are *module-control states*, not implied discharges, magic power drains, or authority to seize controls. Physical isolation and genuine fitted hardware delimit what can be compromised. Real manual propulsion, hull integrity, mass/inventory, and the intrinsic emergency safety sensor are not eligible under this first attack.

## Implemented contracts
`ShipStatusEffects.hpp` supplies a small deterministic `Engine`:
- host-only `inflict_confusion` admission requiring target/source identities, nonzero unique effect ID and seed, finite start/expiry (up to 300 world ticks), pulse cadence (1-60 ticks), strength and defense (0-100), and a nonempty original installed module attack surface;
- the host must separately verify attack range, ownership, permitted combat/cyber path, genuine exposed interface and countermeasure identity; **a boolean sent by a client is not evidence**;
- fixed fitted slot/type keys captured on admission; future replacements in the same slot are not automatically compromised;
- deterministic pseudorandom selection without replacement, with a random count from 1 through a bounded strength-derived maximum on each pulse;
- no unbounded stacking or continual duration refreshing: intersecting effects on one vessel are rejected;
- intensity minus resistance sets the maximum simultaneous selection count; fully resisted and isolated effects do not activate;
- read-only projections of `requested_on` (human/NPC intent) versus `effective_on` (temporary compromised state). Expiry automatically returns to current intent; no stored destructive mutation or free material/ammunition change;
- bounded versioned `ship-status-v1` serialization and strict parser for effect records, deterministic mid-attack replay and invalid-record rejection. This is a **snapshot fragment**, not a new SQLite world. Production must commit it atomically with the canonical world state.

`ShipStatusFitting.hpp` reads the existing `Entity::fitting` rack. Only real hardpoints currently implemented by PLAY-11 (scanner ID 1 and probe launcher ID 2) enter this first projection. Empty slots and reserved future equipment are never fabricated. The current host must explicitly provide pilot intended activation, verified exposed interface and isolation state. The source engine does **not** decide that all fitted equipment is hackable.

## Native regression
`tests/SHIPSTATUS01_ConfusionTests.cpp` checks invalid/duplicate and resistable attack admission, deterministic same-tick state, pulse transitions, isolation, fitting replacements, nonstacking, owner-independent ship scoping, intent restoration after expiration, serialized restart replay and malformed snapshot rejection. It also attaches effects to a real starter colony ship's scanner/probe launcher and checks original hull/ammunition remain unchanged.

## Required production integration — *not complete*
1. Implement an authenticated original-world attack command with *real attack-device* fitting, delivery, cooldown/range, and defensive surface proof, without inventing hardware.
2. Include `Engine::encode()` in the original `Simulation` canonical snapshot and validate decoded effect-to-ship/fitted-slot identity on restore, within the same atomic `PersistentWorld` transaction and world-generation CAS. No separate status database.
3. Route module ON/OFF commands and action execution through `effective_on`. A visual toggle alone is not an enforcement mechanism. The existing starter scanner and probe launcher currently have action-based behavior, not general module activation commands.
4. Connect authoritative state to human cockpit and NPC tactical control, including accessibly represented disruptions, real module schedules and recovery.
5. Verify genuine Windows/Linux native builds, crash/restart, duplicate packet/idempotency, bounded CPU costs, withdrawal/isolation and all existing ship-fitting/persistence regressions.

Keep the runtime status system as a gameplay effect. The effect must never stress the player's real machine.
