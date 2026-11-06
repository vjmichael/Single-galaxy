# SHIP-01F — Two-colony shipyard customer orders and actual hull delivery

Status: **source and native acceptance tests implemented; exact-head CI not yet green.** Stacked on SHIP-01E, SHIP-01D, SHIP-01C, SHIP-01B.2, SHIP-01B.1 and SHIP-01A.1. SHIP-01F introduces the first persistent, fully material-backed customer/supplier relationship between two separate colony ships, including **different owners** at the Simulation/PersistentWorld authority level.

## Why a separate order record is necessary

The real provider's `ShipyardConstruction` owns its NPC workers and assembly slip, and `FreighterBuildOrder` preserves immutable factory provenance. A purchaser cannot simply relabel these records as their own without corrupting the builder's history, factory resources or NPC identities.

The v93 `ShipyardCustomerOrder` is a **customer/supplier contract only**, referencing an existing operational yard and a uniquely reserved future native build ID; it contains neither cargo nor a second ship object.

Stages:
1. **Requested:** Customer owner and source colony ship request an existing operational yard, specifying unique contract/build IDs. The provider is not forced to accept. No materials change hands.
2. **Accepted:** The actual owner of that supplier yard consents. One accepted/funded customer order can occupy the same yard. A customer is not authorized to accept on the supplier's behalf.
3. **Funded:** The customer brings **six actual fabricated modules** into physical proximity of the supplier colony ship (same local celestial frame, stationary ships, within the existing docking/berth radius). In *one copied-state SQLite transaction*, exactly six modules leave customer inventory, arrive at provider, and are consumed by the existing `queue_shipyard_freighter` mechanism. The normal two supplier NPC builders make progress for twelve committed ticks. Any rejection or crash rolls the entire debit/order back; no virtual escrow inventory.
4. **Delivered:** Once the ordinary native job launches **one real uncrewed Entity** with its manufactured ID and 100m³ hold, the customer must return to the *actual hull* at a physical berth to receive ownership. The provider's original build/yard/worker provenance remains intact and immutable. Transferring ownership never invents a second ship.

After delivery, **the customer commissions the hull** from their own colony ship, selecting two of *their* existing available named NPCs, spending their own additional fabricated scanner module, and transferring finite food, water and oxygen. The seller's crew, modules and provisions remain at the seller's ship. The customer's fleet selector recognizes the delivered-and-commissioned hull through the validated customer delivery record even though the original construction source is another colony.

## Native implementation

- `include/galaxy/Simulation.hpp`: `ShipyardCustomerStage` and `ShipyardCustomerOrder`, native post/accept/fund/deliver interfaces, read-only order projection.
- `src/Simulation.cpp`: supplier consent, unique build reservation, actual six-module physical transfer, the **existing** twelve-tick shipyard manufacturing, owner transfer of the single unfinished hull at a verified berth, customer-owned commissioning, v93 canonical serialization/parser, cross-record ownership/material/provenance checks and world swap recovery.
- `include/galaxy/Persistence.hpp` / `src/Persistence.cpp`: four SQLite-atomic copied-Simulation operations, conservation checks and crash-injection support.
- `include/galaxy/FleetSelection.hpp`: delivered owner-vetted customer hull becomes piloted fleet eligible *only after* valid commissioning with real crew/life support.
- `src/colony_live_host.cpp`: authenticated `YARD_CONTRACT_POST|order|yard|build`, `YARD_CONTRACT_ACCEPT|order`, `YARD_CONTRACT_FUND|order`, `YARD_CONTRACT_DELIVER|order`, `YARD_CONTRACT_STATUS`; native stages and read-only per-owner status.
- `live/interactive_relay.py`: same-origin/CSRF, exact-schema actions for the whole customer workflow, no client-supplied owner, NPC identity, unverified hull ID, resource override or bypass flag.
- `viewer/colony.js`: a shipyard purchase panel under the existing shipyard details and strict canonical receipt decoder. Customer can request, supplier can accept, material can be delivered, completed hull can be claimed, and status displays actual build progress; after handover, its build ID is passed to the existing commissioning controls.

## Non-negotiable invariants

- Customer owner and supplier owner authorize **different** phases. No permission to move a supplier's modules, accept their work, or staff a buyer's ship is inferred merely from having a contract ID.
- Six module units are consumed *once*, preserving 12 ore-equivalent units in the original `FreighterBuildOrder` and `Simulation::total_ore()`.
- The provider's recorded yard, workforce and supplier identity never become the customer's. Ownership of the physically delivered hull changes only after factory completion and customer arrival at the berth.
- The customer cannot use the ship before commissioning. No free engine modules, scanner, crew, food/water/oxygen or duplicate hulls.
- v93 snapshots validate cross-references between customer contract, manufacturing receipt, source owner, delivered hull ID and current physical owner. Old v1–v92 saves remain parseable. Transaction failure injection does not commit a half-paid or half-delivered ship.
- The seller's assembly slip remains a **single real assembly slip**, with no parallel unfinished freighter jobs.
- A supplier colony may continue normal work/production while manufacturing the customer hull.

## Acceptance

`tests/SHIP01F_CustomerShipyardTests.cpp` seeds two genuinely **different players** (buyer 8, supplier 7), separate real colony ships and finite asteroid modules. Native acceptance proves:
- The supplier pays four modules for their yard and supplies two actual named builders.
- Buyer manufactures eight modules, pays exactly six, reserves one for their later scanner and never takes supplier material.
- Posting/acceptance/funding by the wrong principal are rejected.
- Injected failure after snapshot writing for funding or delivery restores the **exact original world and generation**; retry succeeds once.
- Twelve real build ticks create precisely one uncommissioned seller-owned physical hull.
- Buyer only claims from a verified physical berth. The buyer's own NPCs, scanner and finite supplies commission it, and the built hull enters the **buyer's** fleet.
- Ore-equivalent matter and NPC population remain conserved, SQLite restart restores the two-sided receipt, and a forged delivered hull ID is rejected during v93 snapshot parsing.

`tests/PLAY13D_PlanetSurveyRelayTests.py` verifies every HTTP action has an exact same-origin/CSRF schema. `tests/PLAY13D_PlanetSurveyNodeTests.js` checks bounded receipt decoding and rejects invented hulls, cross-owner identity and illegal stage transitions.

Dedicated `.github/workflows/ship01f-customer-shipyard.yml` runs CTest on Windows and Linux, plus Linux native host, Python relay and Node UI contract checks; also reruns the earlier SHIP-01C manufactured physical freight and SHIP-01D fleet gates.

**Verification has not passed unless the exact PR HEAD actually completes the native Windows/Linux checks.** Keep draft until then.

## Scope and what remains

This gate supports **customer-funded, manually posted contracts** and *actual finished hull delivery*. The connected browser host currently restricts the live session to its original colony owner; the native two-owner Simulation and Persistence operations are tested separately. General logged-in multi-owner gameplay sessions, independent supplier marketplace advertisement, credit-based contracts, remote transport of the six modules, NPC automated pickup/delivery of entire ships, workforce-aware production queueing across multiple yards, and self-expanding shipyard capacity are **not claimed complete**. They are the natural next industrial automation milestones rather than reasons to fabricate an unsupported result in this gate.
