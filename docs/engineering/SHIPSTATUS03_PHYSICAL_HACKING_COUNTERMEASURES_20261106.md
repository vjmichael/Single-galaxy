# SHIP-STATUS-03 — Physical Hacking Modules, Attack Delivery & Countermeasures

**Source state (2026-11-06):** native implementation committed on a stacked draft; exact-head Windows/Linux CI and independent installed-game acceptance not yet passed. No user-facing module UI or autonomous NPC cyber tactics claimed.

## Technology and inventory

This is Single-Galaxy's original cyber-control warfare, **not EVE capacitor warfare**. It uses the one existing ship physical fitting and colony manufacturing system.

- **Hardpoint type 3: Intrusion emitter.** `Action::ConfigureCyberModule` with `amount=+3` consumes **one existing manufactured module** from the owning colony ship's finite `ColonyShip.modules` inventory. The emitter occupies one of the two original hardpoints; uninstall `-3` returns that original physical module to inventory.
- **Support type 1: Cyber firewall controller.** `ConfigureCyberModule +4/-4` uses one manufactured module and one of the existing support racks; its passive protection subtracts 60 attack-intensity points and halves the status-effect lifetime from 8 to 4 world ticks. Its construction and installation cannot create or clone ore/material.
- The original factory path `Mine → RefineOre → FabricateModule` provides real installed-equipment inputs. No duplicate module database, virtual free ammo or new manufacturing UI is introduced.
- A genuine player or delegated NPC actor remains a physically controlled original `Entity::Ship`. Crew/custody/flight ownership authorizations continue to apply.

## Authoritative attack execution

`Action::HackConfuse` (normal host-journaled original command) enforces:
1. An actually installed, effectively **ON** type-3 emitter and a physically controlled, crew-eligible ship.
2. Real target ship, different owner, undocked and not under native jump/cloak protection; source in open space and ready.
3. The same true system/local celestial frame and maximum **8,000 millimeters in the current local-frame simulation**. This is a prototype combat-range constant; it must be retuned as the game's physical space units mature.
4. One emitted command with the original ID, exact tick, target and outcome recorded through `PersistentWorld`'s existing SQLite transaction and world generation CAS.
5. Five world ticks between emissions, including fully resisted ones; `Entity::last_hack_tick` persists in the canonical world and cannot be reset by removing/reinstalling the emitter.
6. A deterministic, finite 8-tick `Confusion` effect (4 ticks against firewall), pulse every 2 ticks; actual fitted hardpoint identity determines which original module-control states may be inverted. A manually isolated slot cannot be remotely changed.
7. No physical hull damage, teleportation, energy siphoning, arbitrary warp prevention, mass mutation, automatic weapon discharge or denial of manual thrust.

Host-generated production effect IDs occupy a disjoint high-bit namespace from accepted command IDs; snapshots link each to an actually Applied original command at the authentic tick, with real source/target and a durable emission timestamp. Test-only lower-ID status fixtures from SHIP-STATUS-02 remain supported.

**Note:** This is a native in-game electronic-intrusion mechanic. Future anti-crime/trespass jurisdiction must decide how to classify offensive use, and NPC tactics must only activate it when equipped, in range and authorized. Merely being a hostile NPC colony does not supply equipment.

## Counterplay

- An installed support firewall reduces severity **and** duration, not invulnerability.
- `Action::SetCyberIsolation` allows the pilot to physically isolate one of the fitted hardpoints, disabling remote status overrides but retaining the pilot's ordinary requested module state.
- An isolated slot can be reconnected; if the intrusion is still active it may be affected again. Expiration always restores current pilot intent.
- Unlike the status effect, structural damage and capture remain separate native simulation mechanisms.
- A confused *attacking emitter* can itself fail an attack if its effective ON state is false; no magical exemption for NPC ships.

## Durable snapshot v101 and recovery

The existing v100 per-hardpoint requested state is retained. v101 appends two manual-isolation bits and original-ship `last_hack_tick` to each existing entity row, widens the original fitting type validation to accept emitter hardpoint ID 3 and firewall support ID 1, and accepts the two new outcomes and three new actions. Every older snapshot version remains readable, defaulting the new state safely.

The existing `shipstatus` canonical record is reused, not replaced by a separate SQLite ledger. Applied high-bit production attack receipts are checked against original native events on restore, and their resistance/lifetime must match the admitted module effect. Expired historical effect keys may refer to legitimately replaced/removed hardware without blocking restart; **active** status records must still point to genuine installed modules.

The original `Simulation::swap` and `PersistentWorld::step` writer lock commit both ship and effect snapshots together.

## Native regression

`SHIPSTATUS03_PhysicalHackingTests.cpp` verifies finite ore mining/refining/manufacture, fitting and exact material conservation, owner rejection, powered-off emitter, interrupted SQLite snapshot write and independent reopen, a genuine successful hostile attack, a native scanner action denied by Confusion, cooldown, manual isolation recovery while effect is active, restart and `recover_copy`, fully resisted attacks, firewall installation, shortened subsequent effect, strict v101 replay and tampered high-bit attack ancestry rejection, and legitimate refitting after status expiry.

Windows/Linux workflow `ship-status02-authoritative.yml` now also builds and executes the new native test alongside SHIP-STATUS-01/02 and PLAY-10.

## Still to build

A production equipment-crafting UI, inventory presentation, cockpit status notifications, observable NPC combat use and legal case integration are not yet wired. The existing optical/direct/manual flight controls remain physical; visuals must present spoofing accessibly. Future cybersecurity skill/equipment diversity should be grounded in installed modules and the one status engine rather than making a new parallel combat system.

Keep native compilation, exact-head GitHub Windows/Linux regression and installed-game player/NPC acceptance separate. This document describes committed source, **not a completed native validation**.
