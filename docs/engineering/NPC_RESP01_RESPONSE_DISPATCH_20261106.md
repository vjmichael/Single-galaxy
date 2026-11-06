# NPC-RESP-01 — Original NPC flight engine as response dispatcher

**Engineering status (2026-11-06):** real source committed; exact-head native Windows/Linux CI pending.

## Implemented
`NpcResponseDispatch.hpp` uses the existing `PersistentWorld::queue_npc_ship_rendezvous` and `NpcFlightPurpose`. No second AI movement, queue, resource, ship, or passenger system is created.

`dispatch` admits owner-controlled player distress/escort responses using real owner-controlled responding and subject vessels. It returns an explicit status, creating no extra resource or character state. It cannot submit law enforcement, recovery, combat, detention, or cross-faction rescue merely because a client provides a target ship ID.

`dispatch_observed_patrol` allows an owner-controlled patrol ship to approach a **real foreign vessel** that a currently present, duty-eligible, named NPC watch member has actually observed through `colony_defense::observe_local_contacts`. The contact is rederived from the current simulation, rather than trusting caller-provided coordinates or guessed IDs. Its original native pilot, real thrust commands, v97 flight receipt, and SQLite fault/restart are still required. The purpose is Patrol, not an attack warrant. On arrival it has permission only to **hail/request an inspection**, which still needs its own future state machine to execute; no arrest, boarding, cargo seizure, destruction or damage occurs.

## Critical separation of authorities
1. **Physical presence:** real local sensing is not proof of crime.
2. **Dispatch:** a verified observation or authorized owner request can justify movement, but nothing else.
3. **Enforcement evidence:** the current `ColonyConflict` strategic attitude and `ColonyDefense` contact do not constitute a guilt finding. Cross-faction lawful authority must be represented in a separate authenticated and durable incident/warrant system before an arrest.
4. **Ship disabling:** existing `Action::FireTrainingWeapon` works only on uncrewed training drones. It cannot be repurposed to damage the player's ship; new combat rules, limited resources and authoritative physical receipts are required.
5. **Player != ship:** current `FleetSelection` is cockpit/control-plane state, not an independently simulated human avatar. Player rescue, escape pods, boarding, arrest, detainee transfer and custody need a distinct **authoritative and durable** player-character identity/location model. Never fake it by reassigning the ship's owner or by moving NPC child custody records.
6. **Child safety:** guardian and dependent journeys continue only through FLIGHT-06's original validated complete-family SQLite ledger, never through the player's upcoming custody component.

## Native checks
`galaxy_npcresp01_dispatch`:
- rejects wrong-owner rescue, false foreign contact, premature invented legal enforcement and duplicate orders;
- attempts to abort an SQLite response dispatch after writing the snapshot and checks exact world-state equality;
- reuses a real named physical local watch to detect foreign patrol target;
- starts native **two separate NPC** original physical flights for owner aid and foreign contact interception;
- confirms one original rendezvous receipt and no invented ore or people;
- independently restores the v97 canonical state after closing/reopening the SQLite store.

## Next separate gates
- NPC-RESP-02: character embodiment and player-owned emergency distress consent; establish persisted person-vessel location and nonfatal emergency survival.
- NPC-RESP-03: lawful incident evidence and published jurisdiction/warrant, distinct from opinions/contact.
- NPC-RESP-04: physical ship disabling with conservative combat authorizations and actual finite weapons.
- NPC-RESP-05: consent-based rescue, lawful arrest, detention/release, and independent audited player custody.
- Cross-product exact-head CI is not accepted while queued/failing.
