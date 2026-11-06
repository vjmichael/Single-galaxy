# WORK-01C — Original Native Local Delivery and Intercolony Freight Adapters

**Status:** Source, full physical-world regression, CMake registration and Windows/Linux workflow implemented on stacked draft above WORK-01B.3. **Native CI unverified until exact-head workflows pass.** There is no newly authored shuttle, NPC passenger model, carrier, cargo ledger, route teleporter, inventory authority or second persistence store.

## What the player permits

The new `PersistentWorld::delegate_transport_work_order(owner,origin,directive_id,kind,source_order,requires_directive,failpoint)` is a SQLite-atomic authorized *delegation of an already existing original work order*, not a new order posting API. Supported `kind` values:
- `LocalDelivery`: the actual `LocalDeliveryOrder` physically assigned to the owner's colony and an existing planetary site with an operational spaceport and real shuttle.
- `IntercolonyFreight`: the **preplanned** `IntercolonyFreightLeg` attached to the existing local pickup, a specific already-owned, crewed, scanner-equipped physical carrier and a real receiving colony.

The original `post_local_delivery_order` and `post_intercolony_work_order` still create the underlying jobs. The original `plan_intercolony_freight` validates and reserves the real cargo-capable carrier; the coordinator cannot conjure a ship, consent, fuel or job. Delegation requires matching owner, source colony and original order, and can name an earlier valid same-owner/same-origin dependency. For example, `LocalDelivery(77007)` may be the predecessor to `IntercolonyFreight(77007)`; these may share a source ID because they have separate typed receipts. Duplicate noncancelled delegation of the same **kind and source** is denied.

`cancel_work_directive` cancels only future coordinator permission. It cannot retract physical cargo or interrupt a real shuttle already in flight. Conversely, cancelling the original **Posted** local order now atomically cancels still-active matching local/freight delegations; any pending, unsealed freight plan is removed by its original owner as before. This prevents an active coordinator reference to an erased original carrier plan.

## Actual execution and its physical limits

`Simulation::process_local_delivery_board` already processes native local jobs every committed world tick. Its existing selection finds a qualified, presently available *real NPC* shuttle pilot, reserves that physical shuttle and advances real descent, load, ascent and unload flights with exact native receipts. WORK-01C does **not** replace or repeat that flow. The coordinator observes its original `LocalDeliveryStage::Complete` after native unloading and only then fulfills the delegated local work intent. A posted, descending, loading, ascending or delivering order remains unfulfilled, even when the coordinator is authorized.

The preplanned second leg is already loaded by the native shuttle board when the local pickup is truly complete and the carrier is physically berthed with sufficient finite hold capacity. The existing `IntercolonyFreightLeg`, `intercolony_transport_manifest` and real `ColonyShip::goods_kg` are the only cargo records. The coordinator never duplicates a sealed manifest or moves material directly.

For the last leg, an existing owner/host flight command must navigate the original carrier to its **actual distinct destination body**; WORK-01C does **not** auto-submit navigation orders or teleport ships. Once the carrier's original completed-celestial-transit receipt, physical same-frame berth, exact sealed cargo and destination capacity exist, the coordinator calls the **original** `Simulation::deliver_intercolony_freight` in the same copied world step used by `PersistentWorld::step`. The native method enforces owner, actual arrival, physical goods and exactly-once recipient stock transfer. Successful unloading produces the original `IntercolonyFreightStage::Delivered` and `arrival_receipt`; only then is the coordinator directive fulfilled. Until then it reports a compatible existing v94 `WorkBlock` wait state.

The work planner is still read-only: two new typed executors now validate against original local and freight maps. It does not create NPC shifts, synthetic transport progress or alternate custody. Its existing stage priority, dependency, capacity and blocked-source checks still apply. Local shuttle selection remains owned by the original native local board, and a dependency on its coordinator receipt does not halt unrelated previously authorized physical shuttle operations.

## Persistence correction and recovery validation

WORK-01C fixes an inherited v93/v94 snapshot header defect: `Simulation::canonical_state` previously serialized versions 93 and 94 with a v92 label even though `from_snapshot` knows v93/v94, which could erase or reject authenticated orders on restart. The writer now emits the actual v93 or v94 header for all newly persisted worlds. This does not manufacture lost data in any **historically mislabelled** snapshot; such data requires an explicit migration/forensics process, not silent guessing.

The existing v94 `workdirective` record layout is reused with `kind=1` (local) or `kind=2` (freight) and zero commissioning crew/provision fields. The parser disallows crew data on transport directives. Its native source validator now:
- authenticates the original local pickup owner/colony for both kinds;
- verifies an exact, same-owner, same-origin planned freight leg, same resource kind and quantity, if applicable;
- permits a cancelled freight directive to reference a formerly erased unsealed plan, but never considers it fulfilled;
- verifies dependency identity and prerequisite completion in committed state;
- rejects duplicate active delegations per **(task kind, original source)**;
- rejects `LocalDelivery` fulfillment unless the original native local order has physically completed;
- rejects `IntercolonyFreight` fulfillment unless the original freight leg is `Delivered` with an actual arrival receipt;
- continues to validate all old customer-ship order commissioning proofs unchanged.

No new snapshot fields or schema version is introduced beyond correct publication of the already-defined **v94**.

## Native acceptance

The test fixture `tests/PLAY13D_RegionalFacilitiesTests.cpp --delegated-work` reuses the existing real physical scenario that has already validated planetary construction, surface stock, finite shuttle water/fuel, named pilot, first and second planetary pickups, carrier hold, actual celestial travel, SQLite failpoints, restart and conserved matter. It adds:
- wrong owner, nonexistent source, duplicate directive, absent planned freight leg and fault-injected SQLite delegation rejection;
- local completion with actual pilot and two-leg shuttle flight proof;
- original unstarted pickup cancellation updating derived delegation;
- exact sealed manifest and pending arrival, no premature shipment fulfillment;
- forged v94 snapshot completion rejected for an in-flight shuttle and for a freight shipment before arrival;
- **two** existing physical carrier voyages; after the owner drives the carrier, the coordinator completes actual unloading without a new player unload command;
- dependency local→freight (one original order ID, two typed coordinator directives); replay, original recipient stock and native arrival receipt preserved after restart.

CMake test: `galaxy_work01c_native_transport_executors`. Workflow: `.github/workflows/work01c-transport-executors.yml` Windows/Linux native SQLite, the full WORK-01C fixture and stacked WORK-01A/01B.1/01B.2/01B.3 regressions.

## Remaining explicit work

This phase does **not** automate celestial navigation, select arbitrary alternative carriers, assign autonomous route waypoints, grant player-facing UI controls, or broaden owner authority beyond current native dispatch. Host desktop commands to issue these new delegations can be added to the human-facing work board; WORK-01C currently exposes the typed native `Simulation` and transaction-safe `PersistentWorld` APIs.

Next follow-up can add owner-authorized automatic carrier departure/navigation through the existing transit command and custody rules, then extend scheduling to more physical NPC tasks. All actions must use existing physical systems and journals.
