# SHIP-01E — Delegated fleet freight assignments (2026-11-05)

**Implementation committed; real native Windows/Linux CI and installed desktop playtest are not yet passed.** Stacked above SHIP-01D fleet selection, SHIP-01C real manufactured first freighter, SHIP-01B.2 commissioning and existing PLAY-13D planetary logistics.

## Player-facing goal

A player should **not need to pilot every vessel they own**. After building and commissioning a freighter, the player can assign it a straightforward NPC freight contract:

> Pick up 100 kg of nutrients at the player's actual Planet A surface operation and deliver to Colony B.

The player can continue flying their *own selected ship*, mining, or managing their original colony. The delegated NPC freighter uses the real planetary shuttle work board, cargo transfer, four-tick celestial navigation journal and destination inventory; no new virtual shipping layer.

## Native execution and explicit authority

**Existing source of truth:** `PersistentWorld::post_intercolony_work_order(owner,home,site,cargo,kg,order,carrier,receiver)`. This is a single authoritative transaction combining the local NPC pickup work order and a durable `IntercolonyFreightLeg`. The canonical work scheduler subsequently does the finite surface shuttle pickup, exact inventory handoff into `ColonyShip::goods_kg`, carrier transit, receipt and delivery into `ColonyLifeSupport` at the real receiver.

New authenticated native host command: `FLEET_WORK_POST|order|carrier|destination|body|latitude|longitude|resource|kg`. Successful native posting returns `FLEET_WORK_ASSIGNED|order|carrier|destination|generation`; denial returns `ERROR|FLEET_WORK_DENIED` and leaves all resources/crew/receipts unchanged.

This new operation **does not change** `home_ship`, transfer economic authority, switch the active player cockpit, mint cargo, manufacture a hull, fabricate an NPC pilot, or permit a player to dispatch a foreign ship.

The shared preflight `fleet_freight_eligible` requires:

1. A real manufactured, commissioned, owner-controlled hull associated with the current home colony, with real physical ship/frame identity and actual named commissioned crew aboard.
2. A real same-owner receiving colony ship with independent inventory and life-support identity.
3. Carrier distinct from the home colony ship, receiver and **the player's currently piloted vessel**. An NPC order cannot seize an active player's ship.
4. No unfinished freight ledger entries already claiming the same carrier. Busy carriers stay reserved until the canonical job is delivered.
5. A finite reserve of at least **ten actual consumption cycles per onboard crew member** in each of food, water and oxygen. The crew's needs are not ignored just to satisfy a freight delivery.
6. Existing native site, local celestial frame, source/receiver separation, real shuttle assignment, hold capacity and pending transit validation. These are checked by the underlying existing atomic work-order and freight engines, not replaced by the UI.

The existing host NPC freight scheduler now skips the player's active cockpit when deciding which loaded vessel may leave. Native `FLEET_SELECT` rejects reserved freighters **before** modifying session pilot selection; denial cannot silently steal control away from a previously selected vessel.

## Live UI and HTTP

The **Your Fleet · Active Ship** panel now also contains **NPC freight assignment** with native fleet-derived carrier selection and source planet/cell, resource (water, food, nutrients), kilograms and Colony B receiver ID. One click posts a new unique work-order ID and waits for the authenticated native receipt; there are no speculative gains in cargo stock or colony inventory.

The same panel displays the actual `INTERFREIGHT_ITEM` stages from the existing work ledger: awaiting pickup, sealed cargo loaded, or delivered with the real arrival receipt. Refresh re-reads the existing authoritative freight board.

HTTP action `colonyFleetWorkPost` requires an exact same-origin CSRF-authenticated payload `{order,carrier,destination,body,latitude,longitude,resource,kg}`. No client-supplied owner, crew readiness override, bypass, cargo ledger, simulated arrival, or source inventory mutation is accepted. Invalid, boolean, overflowing, extra or forged fields fail closed.

## Regression/acceptance evidence required

- `tests/SHIP01D_FleetSelectionTests.cpp` now creates a **real second destination colony**, then verifies uncommissioned, foreign, active-piloted and invalid receiver fleet eligibility. The actual commissioned freighter becomes dispatchable **only while the player pilots another ship**.
- `tests/PLAY13D_RegionalFacilitiesTests.cpp --manufactured-only` reuses the complete existing Planet A surface production, shuttle work board, sealed 100 kg/200 kg cargo, physical transit, Colony B consumption inventory, SQLite rollback/restart and conservation check. Added assertions verify the real freighter is eligible before posting the NPC work order and becomes unavailable for double-assignment immediately after the canonical work order is committed. The player remains aboard the home colony cockpit.
- `tests/SHIP01D_FleetHostTests.py` checks the real authenticated native host refuses fake/founding-ship carrier dispatch from a fresh save.
- `tests/PLAY13D_PlanetSurveyRelayTests.py` adds valid HTTP dispatch and rejection of forged token, owner field, missing/boolean/noncanonical IDs, invalid cargo and quantity, fake crew and teleport overrides.
- `tests/PLAY13D_PlanetSurveyNodeTests.js` checks exact native dispatch receipts, existing physical freight lifecycle and malformed receipt rejection.
- `.github/workflows/ship01e-fleet-work-dispatch.yml` configures native **Windows + Linux** CTest and **Linux** real-host/relay/JS contract. All must pass against exact PR head; a queued workflow is **not** a green acceptance result.

## Deferred

This step delegates *one physical shipment to one existing commissioned freighter*. Fleet-wide route optimization, multiple orders per hold, player-to-NPC paid work contracts, NPC career qualifications, autonomous refueling, multi-system travel, ship loss compensation and automatic construction of additional shipyards are separate features. This implementation deliberately does not confuse a posted work order with a completed journey.
