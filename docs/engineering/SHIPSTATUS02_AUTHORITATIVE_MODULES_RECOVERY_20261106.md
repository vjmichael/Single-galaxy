# SHIP-STATUS-02 — Authoritative Module Execution and Persistence

**2026-11-06 implementation:** stacked on SHIP-STATUS-01, native source/test/CI committed. Exact-head Windows/Linux CI not yet accepted. This gate implements real native *hardpoint command* enforcement and canonical SQLite persistence; it does NOT falsely claim a fitted hacking weapon, a live attack UI, or an NPC tactical ability.

## Core architectural decision

The original `Simulation::Entity::fitting` is still the physical source of module identity. The original `Simulation` owns one `ship_status::Engine`; there is **no second world, ship entity, or SQLite status journal**. Both are encoded into **galaxy-kernel-v100** canonical snapshots and are committed by the existing `PersistentWorld::Store::commit` generation-CAS transaction. An interrupted commit leaves neither partially installed effects nor a partially advanced world.

A fitted hardpoint has:
- `hardpoint_requested_on[2]`: durable ship operator's requested ON/OFF controls, true by default for backward compatibility;
- `hardpoint_effective_on(ship,slot)`: authoritative, deterministic *projection* of requested state plus active Confusion, with the exact original slot/hardware type;
- `Action::SetHardpointPower` (slot*2 + on bit in `amount`): owner-controlled native command, respecting existing person-aboard and NPC-flight exclusivity checks, original simulation queue and immutable command receipts;
- `Outcome::ModuleOffline` when a fitted scanner or probe launcher is effectively OFF. This is checked **inside** original `SurveyLocal` and the original probe/drone action executor, not only in the HUD.

An installed scanner or probe launcher does not spontaneously issue scan/drone actions merely because Confusion switches its effective state ON: those existing equipment actions still require their normal controls, targets, resources and authorization. Future continuous modules will consume this same projection at their own execution path. No module is fabricated or inventory changed.

## Snapshot v100 and recovery

- Legacy snapshots v1–v99 decode with original implicit ON defaults and no status effects.
- v100 serializes two owner-intent bits on each original entity row and `shipstatus` records for real physical source/target ship IDs, canonical source tick/expiry, deterministic seed, pulse, intensity/resistance and fixed module identity.
- Restoring v100 verifies the strict status parser, capacity, temporal scope, source/target and actual fitted module keys. Rejects malformed or forged records and reserializes canonically.
- The `Simulation::swap` path includes the status engine so SQLite COMMIT and in-memory publish cannot diverge.
- Status expiry is computed from the committed world tick: after expiry the *latest operator-requested setting* wins; physical hull, momentum, crew, weapons and materials are unchanged.

## TEST-only attack setup (not player accessible)

`PersistentWorld::submit_testing_ship_confusion` is a clearly named **trusted test fixture**. It verifies that source and target are real different-owned physical ships, share the same system/local frame, lie within original physical test weapon range and refer to actually fitted vulnerable hardware. It atomically commits the effect alongside the world. It **does not** imply working hacking hardware, proper cyber access, or lawful combat authority. No production network action invokes it. A genuine future hacking attack will require real manufactured/fitted equipment, resource/cooldown checks, an authenticated operator, real attack delivery and permissive legal/combat context before using the original effect engine.

## Native acceptance

`SHIPSTATUS02_AuthoritativePersistenceTests.cpp` covers:
1. Real original colony ship and genuine starter scanner/probe launcher.
2. Reproducible randomized Confusion seed and successful suppression of native `SurveyLocal` (not UI-only).
3. Native operator ON/OFF command; temporary involuntary override; restoration of current operator intent at expiry.
4. Invalid and duplicate attack admissions, invalid hardpoint commands, owner/custody/flight controls, and no material or ammunition changes.
5. Injected SQLite `AfterSnapshotWrite` failure, independent reopen and no partial publication.
6. v100 live-state restart, `recover_copy` and independent recovery, real native action after expiry, tampered status record rejection.

Windows/Linux CI builds SHIP-STATUS-02, SHIP-STATUS-01 and preceding PLAY-10 exploration regression in one native host environment.

## Known constraints (not closure claims)

- Existing PLAY-11 has only scanner ID 1 and probe launcher ID 2. No weapons/ECM/hacking module activation path yet, so this gate must **not** invent equipment or drain power.
- One attack cannot be stacked repeatedly; policy records remain bounded (2048 maximum). A future compacted tombstone scheme is required for unlimited long-running worlds while preserving unique event IDs.
- Original periodic probe autonomous movement and sensor report custody remain independently physical; turning off the launcher does not delete or teleport probes already deployed.
- Production player HUD and NPC tactical readers must display `hardpoint_effective_on`, not assume the requested setting equals operational state.
- Native CI and independent installed-game acceptance are separate: source committed does not mean verified live combat.
