# PLAY-13D.4G — Real intercolony freight vessel handoff (2026-11-05)

## User intent

The player can instruct NPCs to **collect 500 kg of nutrients from Planet A and deliver it to Colony B**. The existing PLAY-13D.4F work board already executes a real site-to-parent-colony-ship shuttle trip. This increment connects a **completed** pickup to a distinct, **already physically present crewed intercolony vessel**, which moves goods through the existing native celestial transit. No other source inventory or destination colony is credited until a committed physical transfer happens.

## What the code now does

1. **Completed local work is the source receipt.** `Simulation::load_intercolony_freight(owner, local_order_id, carrier_ship, destination_ship)` only accepts a completed local delivery from the authorized origin ship. No arbitrary resources can be posted as a new cargo supply.
2. **Real carrier custody.** The source ship, carrier and destination must be three distinct existing same-owner colony ships, with physical life-support/ship inventory. The origin and carrier must occupy the same celestial frame and be within the real 2.5 km ship berth radius, in space and not jumping. The destination is a separate existing colony in another actual celestial frame. A real existing crew and exploration scanner on the carrier are required. No ship is spawned or silently purchased.
3. **Finite sealed cargo.** The source's `ColonyLifeSupport::{water,food,nutrients}_kg` stock is debited and the carrier's `ColonyShip::goods_kg[freight_*]` physical hold is credited. `Inventory::Catalogue` has explicit 1 L/kg sealed freight items so existing hold capacity is enforced and `Simulation::total_ore()` still conserves the same physical mass. These goods **are not available as generic food/nutrients for life-support recipes in transit**. A second active sealed order cannot book the same hull. The `IntercolonyFreightLeg` record is a receipt, not another inventory.
4. **Existing travel engine.** `INTERFREIGHT_DISPATCH` passes the selected real carrier into existing `PersistentWorld::begin_celestial_transit` and targets the destination ship's actual registered celestial body. That engine requires a scanner, checks physical route eligibility, journals the pending transit, and atomically commits `complete_celestial_transit` at due tick. Neither body position nor arrival is accepted from the browser as evidence.
5. **Verified delivery.** `deliver_intercolony_freight` requires the same carrier to be co-located, within real berth range of the receiving colony, after a committed successful celestial transit **by that carrier**, which postdates the handoff. The sealed item is removed from the carrier's hold and credited to the destination's existing life-support stock in **one SQLite world commit**. A completed order cannot deliver a second time. If the destination ship moves, or transit is not complete, stock remains on the carrier.
6. **Durable restart.** v89 adds `interfreight` canonical records referencing the existing local order, actual carrier and source/destination identities, quantity, origin body, load tick and verified arrival receipt. The parser rejects orphans, duplicated active carrier custody, forged receipts and cargo missing from the physical hull. Old v1-v88 snapshots remain supported; v89 is activated by the first real load. Failed SQLite writes roll back the whole transfer.
7. **Typed universal manifest.** `intercolony_transport_manifest` derives a read-only `transport::Manifest` with `Mode::OtherVehicle`, `GoodKind::Resource`, actual ship ID and sealed goods, using the same contract as planetary shuttles. No alternate shadow roster or resource service.

## Native command and desktop boundary

The existing authenticated relay exposes `colonyFreightLoad` (order/carrier/destination IDs), `colonyFreightDispatch` (order ID), `colonyFreightDeliver` (retry berth verification) and `colonyFreightStatus` (read-only canonical receipts). The host explicitly binds each to the authenticated player and its current local work-board colony ship. New `INTERFREIGHT_*` telemetry comes only from native receipts; a relay HTTP 202 acknowledges command submission, **not** a successful move or delivery. The GUI asks for IDs of real preexisting ships, displays live shipment status and does not create a freighter in JavaScript.

## Native regression

The extended native `PLAY13D_RegionalFacilitiesTests.cpp` creates three actual genesis-funded crewed hulls (not an invented runtime freighter), physically moves the parent/other ships into separate frames, runs the prior work board, transfers a completed nutrient lot to the carrier, checks sealed `transport::Manifest` and unchanged global mass, rejects early/wrong-owner/duplicate delivery, commits a different actual celestial transit, injects SQLite load/unload failure, physically credits the target, and reloads the full v89 state from SQLite. Node and Python relay tests check command and telemetry schemas. The existing Windows/Linux PLAY-13D native workflow is the acceptance gate. **Code pushed is not proof of pass**.

## Deliberately open / not claimed complete

- This is a **real existing crewed colony ship used as a freighter**, not yet a newly **player-manufactured dedicated cargo hull**. The current one-colony-ship starter world has no independent freighter to assign; shipbuilding and outfitting are still needed for normal single-player availability. Do not imply a new vessel is automatically provided.
- A separate NPC pilot identity is not yet assigned to this intercolony hull by a new scheduling adapter. It requires a real crewed ship; its physical transit is host-supervised, not simulated conversation text. A dedicated NPC job assignment/arrival policy will be a further gate.
- The destination in this first increment is an **actual colony ship with life-support inventory**, not an arbitrary planet-side port or distant station inventory. Moving cargo from Colony B's ship to a regional warehouse requires existing or future local facilities and delivery adapters.
- Cross-system routes, customs, insurance, partially filled freight containers and player-defined multi-stop fleet schedules are deferred. Never teleport goods between frames; if a physical route does not exist, leave the shipment aboard its vessel.

## Validation gate

Keep PR as a stacked draft until Linux and Windows native compilation and CTest, relay and Node coverage, plus actual restart/rollback assertions pass at the exact current SHA.
