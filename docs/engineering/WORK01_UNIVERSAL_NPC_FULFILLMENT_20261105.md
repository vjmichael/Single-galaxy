# WORK-01A — Universal NPC Work Board + Native Delegated Fulfillment

**Status:** Source, tests and native GitHub workflow committed on a stacked draft branch. Exact HEAD Windows/Linux CTest and HTTP/browser smoke must pass before calling this gate accepted.

## No rewrites unless required

Existing local delivery, intercolony freight, shipyard construction, manufactured freighter and customer/supplier shipbuilding subsystems already implement real authority and physical effects. They **remain unchanged as the source of truth** for transport, inventories, workers and manufactured hulls.

WORK-01 introduces a shared planning and delegation layer. It never creates a competing inventory, second NPC roster, synthetic cargo manifest, fake ship, synthetic finished-building entry or independent resource debits. No separate job database.

### Five typed native work adapters

`Simulation::universal_work_board(owner,colony)` returns read-only `WorkBoardItem` entries for:
1. **LocalDelivery** — original `LocalDeliveryOrder`, actual surface shuttle pilot and delivered stock.
2. **IntercolonyFreight** — original `IntercolonyFreightLeg`, physical freighter, sealed hold and committed arrival receipt.
3. **ShipyardConstruction** — original `ShipyardConstruction`, real NPC workers and eight construction ticks.
4. **FreighterBuild** — original `FreighterBuildOrder`, 6 already-debited modules embodied in the hull over twelve ticks.
5. **CustomerShipOrder** — original `ShipyardCustomerOrder`, supplier consent, customer-funded manufacturing, actual hull handover.

The board maps original canonical statuses to **Queued / Executing / Completed / Cancelled**, retaining original progress and order identity. There is no duplicated mutable job state. Identical numeric source IDs across different types are disambiguated by the `WorkTaskKind` enum. Owner and original colony filter all projections. This is a single UI board even while different native subsystems own their effects.

### v94: the minimal additional persistent information

A `WorkDirective` records only *delegation consent*: ID, owner, colony ship, typed source order, optional earlier dependency, explicitly approved commissioning NPC IDs and finite provision quantity, active/cancelled/fulfilled state and bounded wait reason. No cloned workers or inventories. Persistent entries belong to the authoritative `Simulation`, versioned `galaxy-kernel-v94`, and committed through existing `PersistentWorld` copy-on-commit SQLite journal.

`delegate_customer_ship_order`: the **buyer** can authorize the coordinator to act for a *specific existing customer contract* only. A supplier cannot authorize it for someone else's customer. The source contract is unique per active/fulfilled delegation. Optional dependency points only to an **existing same-owner, same-colony earlier ID**, making dependency cycles impossible. To authorize commissioning, the buyer must name exactly **two distinct existing NPC identities** from its real colony and a finite provisioning quantity 10–1000 kg. Entering 0/0/0 authorizes *only purchase funding and uncrewed hull collection*. A directive can be cancelled by the original owner before completion. Cancelling stops **future** automation; it does not reverse already performed physical effects or erase durable origin evidence.

`Simulation::process_universal_work()` runs within the same existing world tick and SQLite commit. It preserves deterministic map-ID order, respects previous directive completion, and does not invoke **any** independent cargo/matter or NPC mutations. Its first executable adapter is **CustomerShipOrder**:

- Await real supplier acceptance; no supplier authorization is inferred.
- When accepted, call original `fund_shipyard_customer_order`. Customer and supplier must be at a verified physical berth and six actual customer modules must exist. This original gate consumes six modules and queues the original seller-owned build with existing NPC builders.
- Wait for original **12 physical build ticks**.
- Call original `deliver_shipyard_customer_order` only after a real completed hull is available at the buyer's physical berth.
- If and only if explicitly named two real buyer NPCs plus finite supplies, invoke original `commission_shipyard_freighter`. It consumes a further existing buyer scanner module and transfers existing buyer food, water, oxygen and NPC custody. Complete only after the exact recorded commissioning crew and provision amount match the original immutable receipt.
- Mark directive fulfilled only with original native completion proof.

The scheduler intentionally does **not** pilot customer/supplier ships to a yard, mine missing ore, authorize a supplier, generate a market purchase, or perform other work types autonomously. A blocked directive waits without fabricating missing dependencies.

### Security/recovery invariants

- New manual control commands `WORK_DELEGATE_SHIP|id|contract|crew1|crew2|kg|dependency`, `WORK_CANCEL|id`, and `WORK_STATUS`. Native principal comes from the authenticated host, not the browser. Strict relay same-origin and CSRF validation, exactly typed JSON fields; no browser-supplied owner, worker capability, resource balance, credit, hull ID or skip-safety flag.
- Customer authorization scoped to the original buyer colony; supplier authorization stays with the original supplier. The same NPC identity cannot be invented or double-booked.
- Work source `id` is not a claim that a physical operation completed. The coordinator examines the real original source receipts.
- Durable v94 parser verifies type, bounds, chronology, dependency ordering, source owner/colony provenance, unique active delegation and **physical completion proof**. A fulfilled directive with no delivered hull, forged NPC commissioning receipt or foreign owner is invalid.
- Atomic SQLite failpoint rollback for new delegation creation. A native tick is already transactional; progress, material debit, NPC custody and directive completion publish together or not at all. Replay does not rerun completed or revoked jobs.
- Read-only display uses actual `Simulation::universal_work_board` and `work_directives()`, not local browser counters or fake client-side finish signals.

### Native test / release gate

`tests/WORK01A_UniversalFulfillmentTests.cpp` creates two real owner IDs (7 supplier, 8 buyer), physical ships, finite ore, actual named builders and NPC crew. Native acceptance:
- Separate original supplier shipyard is constructed with paid modules.
- Customer manufactures eight modules, posts order, seller explicitly accepts. **Without delegation, no automatic debit happens**.
- Failed SQLite fault injection during delegation leaves exact canonical bytes, generation and resources intact.
- Foreign authorization, missing/forged dependency, duplicate and invalid NPC crew are rejected.
- The buyer explicitly authorizes its own NPCs and material; next native step consumes exactly six modules into ordinary supplier-owned build.
- Twelve real ticks construct one supplier freighter, customer takes physical ownership only at a verified berth, and the buyer's exact two named NPCs, scanner and finite stocks commission it in a subsequent tick.
- Reads five typed existing-work adapters, verifies original builder and build provenance, no extra NPCs, and unchanged conserved ore.
- Cancels a separate pending delegation and proves it does not pay or create the second hull.
- v94 canonical roundtrip, forged original source denial, and SQLite restart/replay without duplicate ship/crew.
- Browser/relay tests check exact typed native event receipts, owner authentication, CSRF, omission/extra field denial, unsafe payload rejection.

Dedicated `.github/workflows/work01a-universal-fulfillment.yml` runs C++/SQLite on Windows + Linux, including preexisting SHIP-01F, SHIP-01B.2, SHIP-01D regressions; native authenticated host, Python relay and Node protocol on Linux.

### Next phases, not silently claimed done

**WORK-01B.1 now implemented as source (CI unverified):** [Typed Capability & Canonical Planning](WORK01B_CAPABILITY_PLANNING_20261105.md) adds five typed schema entries, stable physical-stage/age scheduling, explicit dependency evaluation, and ephemeral same-tick real-NPC commissioning collision protection without a new snapshot version or inventory. General worker skill matching, site capacity, emergency overrides, configurable priority and installed desktop remain open.

**WORK-01B remaining:** capability registry with typed input/result schemas per original authority; dependency graph planning and scheduling fairness; explicit reservation policies that do not duplicate existing native escrows; priority/urgency/worker availability, emergency preemption, finite worker and machine capacity. Shipyard supplier approval can be a separate opt-in delegation, but never blanket unauthorized acceptance.

**WORK-01C:** safe executor adapters for already-existing local shuttle orders, physical freight, mining, agricultural production, factory crafting and maintenance; some are already native-automatic and need only queue integration.

**WORK-01D:** customer request creates legal demand; available inventory/transport/warehouses and supplier operations satisfy it through actual original movement receipts; ship hull pickup and commissioned delivery travel.

**WORK-01E:** NPC-generated needs and optional owner-approved facility expansion; demand can justify additional actual paid shipyards instead of magically adding capacity.

**WORK-01F:** deterministic prioritization, bounded congestion, crash/failure matrices, workforce and family custody, life-support emergency override, real multi-owner sessions.

The existing browser/relay host remains single-owner, so **multi-owner HTTP sessions are not verified** by this PR. The two-owner exchange and independent NPC rights are exercised by the real native Simulation/PersistentWorld test. Keep draft until exact-head native CI passes.
