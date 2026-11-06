# SHIP-01 — Player-built shipyards, NPC shipbuilding, and intercolony ship orders
**Design decision:** 2026-11-05  
**Status:** SHIP-01A.0 read-only preflight locally verified on archived v66. SHIP-01A.1 authoritative construction, embodied materials, native timed labor, SQLite v90 persistence, host commands, relay and UI are implemented on the next draft PR, but exact-source C++ acceptance remains OPEN. Dedicated station-grade assembly berth, power, intercolony ship sales and NPC contracts are NOT implemented. SHIP-01B.1 manufactures a real hull; SHIP-01B.2 now implements named-NPC, paid-scanner and finite-supply commissioning in a stacked DRAFT, but exact-head native and Windows acceptance remain OPEN.  
**Dependency:** PLAY-13D.4G PR #50 (physical intercolony freight); PLAY-13D.4F PR #49 (local NPC work board).  
**Purpose:** Close the missing ship-availability and industrial-economy part of the freight gameplay loop, without conflating implementation with validation.

## Binding gameplay intent

The player starts with the finite founding colony ship and no free prebuilt station/gate/shipyard. The player may personally command a construction project or delegate it to existing NPCs. A functional colony shipyard is real infrastructure built from collected/refined/machined materials, specialized modules, power, time and available NPC labor. It produces **actual new authoritative ship hull entities** (not UI labels or contract records). It must be able to build cargo, mining, colony logistics, escort and later specialized ship classes using buildable blueprints; the first playable vertical slice builds a small freighter. NPCs can operate the yard under a chosen scope of delegated authority.

Other real colonies can post **shipbuilding work orders** to a shipyard colony's public or bilateral work board. The shipyard may accept, prioritize, fulfill, deliver and receive payment under player/NPC governance policy. Demand can make a colony realize it needs more productive capacity; NPC managers may **recommend** an additional yard or, if explicitly authorized by the player/colony policy with a budget limit, queue a second real infrastructure build. The goal is an emergent specialization and supply-chain economy, not a script that magically doubles capacity or spawns ships.

## Separate finish lines

**Freight technical verification** is not blocked by SHIP-01. PR #50 must first pass exact-head Linux and Windows native build and CTest, real-world live host + HTTP relay + JS telemetry, canonical snapshot/SQLite kill/restart, permission/owner/capacity/proximity/physical-transit checks, duplicate IDs and resource conservation, all at the same exact HEAD. Only then call PLAY-13D.4G technically accepted. A queued GitHub job is never a pass.

**Freight playable completion** also requires a path from the founding player's available resources to a constructed, crewed, fueled/stocked, dispatched freighter, and actual delivery to another colony. The current PR #50 carrier is a preexisting distinct **crewed colony ship**, not a buildable dedicated freighter. That shortcut is an integration adapter, not an industrialized player loop.

## Construction and ship lifecycle — SHIP-01A/B

1. **Player-directed site choice:** Request a yard at an existing colony that the player governs. Recommend an **orbital assembly berth** for ship launch; planetary industrial workshops/spaceports can manufacture modules and route them to the orbital berth. Do not require a full rendered surface. The player places/authenticates construction; NPCs may execute delegated work.
2. **Real prerequisites:** The chosen colony must possess the existing industrial chain, actual material inventories, power, storage, skilled available NPCs, an authorized orbital construction footprint and sufficient transport for any missing inputs. A design bill of materials references existing mineral/refining/manufacturing categories; numerical recipes should be balanced separately, not invented as already implemented.
3. **Work-order construction:** The yard build consists of ordered, cancellable, recoverable work stages. Every committed stage consumes its actual inputs exactly once and generates durable progress with explicit receipt/owner. No material vanishes from another colony or appears at a remote worksite.
4. **Real yard capacity:** Yard has integer construction slips, assembly throughput, on-duty qualified workers, power consumption, finite inventory buffers, queue and maintenance. One slip can host only one active build at a time; queued orders remain distinct and cannot borrow missing workers or resources.
5. **Actual ship manufacturing:** A blueprint specifies supported hull class, cargo/passenger capacity, module slots, mass/parts, crew requirement, construction time and factory inputs. The yard converts actual reserved/consumed stock into one authoritative ship entity only on successful completion of a guarded commit. Constructed hull initially is **uncommissioned**, with no phantom crew or free cargo.
6. **Commissioning:** Assign ownership/control, register hull identity, fit existing modules, provision fuel/life support as required, assign named available NPC crew with legal physical custody and capacity checks, and set real orbital location. Ship can move only after approved commissioning. No duplicated people, goods, or IDs across restart.

## Physical logistics — SHIP-01C

A manufactured freighter is a genuine compatible carrier in the shared universal transport model, with a real cargo compartment and ship travel engine. The original PLAY-13D.4G flow remains: Planet A site harvest/extraction -> NPC shuttle -> origin ship/port -> physical freighter load -> real navigation -> Colony B berth -> destination stock commit. A building order does not teleport a finished hull to the buyer: the ship must be handed over at a valid physical berth or delivered by an authorized transit. Remote inventory counts and local exploration must agree. Start with same-system distinct celestial frames; extend cross-system only when a proved route exists.

## Colony shipbuilding commissions — SHIP-01D

A commissioning colony posts `ShipBuildOrder` to a yard's board: globally unique order ID, buyer, shipyard owner, selected real yard/slip, blueprint variant, number of hulls, input sourcing terms, maximum budget/price, priority, delivery endpoint, accepted/deferred/rejected status, deadline and revision. The yard must explicitly accept or an authorized delegated policy must accept within a bounded budget and queue.

- **No double selling:** One manufactured hull is referenced by at most one unfulfilled sale/delivery receipt. Order ID and hull ID are distinct. Transfer requires a committed origin and buyer ownership receipt and safe berth, plus payment/credit settlement rules; an offer alone conveys no ownership.
- **No speculative materials:** Inputs are physically sourced from the buyer's authorized shipment or the yard's finite stock. Escrow/reservation does not create goods; cancellation releases only still-unspent inventory. Already consumed production stages must be reconciled according to policy.
- **No free workforce:** Every production tick consumes available committed NPC labor; injuries, migration, life-support pressure and local schedules can slow or stop the queue. Existing NPC identity/guardian and transport rules remain authoritative.
- **Market rules:** Real yards can set order acceptance policy, commissions/markups, backlog and capacity. Colonies may specialize and source metals or components from other player/AI colonies. A shortage causes a real supplier work order; not a fabricated delivery.
- **Buyer control:** Initial single-player flow is player-owned colonies placing instructions. Later independent colony governments may commission, accept or reject orders according to their actual governance and economy; the player is never given administrative authority over independently controlled colonies.

## NPC delegated shipyard management — SHIP-01E

Delegation levels, separately authorized by colony policy:
- **Manual:** player explicitly posts every build and new-yard construction order.
- **Operate existing yard:** manager may schedule existing jobs, allocate qualified workers, choose available slips, request finite deliveries, and pause unsafe work.
- **Contract authority:** manager may bid/accept known ship orders inside treasury, capacity, reputation and material limits. Orders beyond scope require approval.
- **Expand facilities:** manager may propose or, only if separately delegated, post a *real funded construction* order for another yard/slip after sustained backlog and feasibility checks. A proposal is informational; it cannot spawn infrastructure.

The opportunity metric should be explainable: current backlog ship-build hours vs available slip-hours, expected input deliveries, NPC workforce, existing utilization, spare funds and expected demand. A shipyard is not always profitable; low demand, limited power and worker scarcity can make an expansion irrational. NPCs should have no infinite-money, infinite-parts, automatic-construction or self-granted authority loopholes.

## SHIP-01B.1 — First finite manufactured freighter (draft)

The first physical freighter hull implementation is now in a stacked draft branch; see [SHIP01B1_FIRST_PLAYER_MANUFACTURED_FREIGHTER_20261105.md](SHIP01B1_FIRST_PLAYER_MANUFACTURED_FREIGHTER_20261105.md). A real operational SHIP-01A yard debits six manufactured modules, uses its named workers for twelve committed ticks, then creates one new canonical uncrewed Entity with a 100 m³ cargo hold in the exact parent local frame. A v91 immutable manufacturing receipt links the order to the hull, and SQLite failpoint/restart tests have been written. The actual new C++ native test has **not yet passed at exact head**; the finished hull is **not yet crewed, fitted, or authorized to transport cargo** until a real commissioning gate. No station-grade power/slip or ship market is being claimed complete.

## SHIP-01E — delegated NPC freighter work orders (draft)

[SHIP01E_DELEGATED_FLEET_FREIGHT_20261105.md](SHIP01E_DELEGATED_FLEET_FREIGHT_20261105.md) connects the actual commissioned fleet roster to the existing native planetary pickup and intercolony cargo work board. The user can assign a real freighter without piloting it; the host rejects the actively piloted hull, uncommissioned/stale/foreign/reserved carriers, foreign receivers and insufficient life support. Existing real shuttle pickup, finite cargo handoff, celestial voyage and receiver inventory remain authoritative. A new fleet UI panel, exact HTTP protocol, native tests and Windows/Linux acceptance gates are committed. **Verification pending**, not marked completed.

## SHIP-01D — owner-safe fleet selection and active cockpit (draft)

[SHIP01D_FLEET_SELECTION_AND_PILOT_CONTROL_20261105.md](SHIP01D_FLEET_SELECTION_AND_PILOT_CONTROL_20261105.md) separates the selected piloted ship from the immutable home colony industry/work-board/resource authority. Manufactured hull selection requires real commissioning, owner, named onboard crew, scanner and frame. The native host routes thrust/yaw/physical travel to the selected hull while the local map/camera focuses that ship, and the browser provides a fleet roster, switch/home controls and per-hull heading/crew/supplies. Unauthorized/unfinished hulls are rejected. Both Windows and Linux exact-head native tests, plus real host/relay/browser acceptance are **configured but not yet green**. Selection is a validated presentation/control-plane preference, never a source inventory or custody mutation.

## SHIP-01C — actual manufactured hull flies Planet A → Colony B (draft)

[SHIP01C_FIRST_MANUFACTURED_FREIGHT_DELIVERY_20261105.md](SHIP01C_FIRST_MANUFACTURED_FREIGHT_DELIVERY_20261105.md) adds a separate real SQLite CTest that **does not seed a freighter**. It manufactures one from real source materials, commissions it using the source colony's existing named crew, a paid scanner and finite life support, then reuses the entire previously implemented planetary extraction, shuttle pickup, work-order auto-load, celestial ship transit and Colony B physical delivery flow with the newly minted `EntityId`. An independent 300-second native test is registered and Windows/Linux exact-head CI is configured. This is **source implemented, not yet runtime-verified**. Do not confuse implemented test coverage or queued workflows with passed playable completion.

## SHIP-01B.2 — real freighter commissioning (draft)

See [SHIP01B2_REAL_FREIGHTER_COMMISSIONING_20261105.md](SHIP01B2_REAL_FREIGHTER_COMMISSIONING_20261105.md). The v92 implementation provides an owner-authorized, short-berth transfer of **two existing named NPCs**, one **already fabricated scanner module**, and **finite food/water/oxygen** aboard the exact v91 manufactured hull. It records immutable crew/stock provenance, conserves world matter, and uses SQLite copy/commit/swap recovery with explicit failure injection. The native host, HTTP relay and temporary browser have commissioning controls and committed status telemetry. These changes are **committed, not native-verified**; the full Planet A→Colony B delivery aboard this hull remains SHIP-01C.

## SHIP-01A.1 — authoritative construction implementation in progress

See [SHIP01A1_AUTHORITATIVE_CONSTRUCTION_20261105.md](SHIP01A1_AUTHORITATIVE_CONSTRUCTION_20261105.md). A draft branch now has exact-material module debit, canonical 8-unit embodied yard structure, eight native committed construction ticks by two named NPCs, v90 snapshot and SQLite fault/restart test, and the authenticated native/HTTP/JS UI. The test target has been committed but NOT compiled against the exact v90 source in this local environment. Do not call this verified.

## SHIP-01A.0 — first native test implemented on branch

`include/galaxy/ShipyardConstructionPreflight.hpp` provides a **pure read-only** construction admission check against the real `Simulation`: authenticated colony ownership, existing live colony ship, populated life-support, **four pre-fabricated construction modules** from the colony's authoritative inventory, and two distinct named, physically available NPC workers. No construction components are manufactured or deducted by this preflight and no shipyard entity is created. The provisional 4-module cost is a first native test fixture, not the final production recipe; a real power/assembly/workforce and bill-of-materials implementation is still required.

`tests/SHIP01A_PreflightTests.cpp` actually mines ore, refines it, and fabricates modules through real Simulation commands rather than seeding the desired output. It checks missing stock, wrong owner, invalid colony, duplicate worker IDs, unknown and foreign workers, unchanged canonical state after preflight, and read-only restart parity. This is registered as `galaxy_ship01a_preflight` in CTest.

**Local verification (2026-11-05):** an archived `SingleGalaxy_COLONY_AUTO04B_v66_2026-11-03.zip` native checkout was configured and compiled on Linux with GCC and SQLite, adding the new preflight source and test. `ctest -R '^(galaxy_ship01a_preflight|galaxy_kernel_tests|galaxy_colony_auto04b_authenticated_relay)

- **SHIP-01A:** Real player-funded orbital shipyard construction and persistence on a chosen colony; reject unauthorized, underfunded and duplicate builds.
- **SHIP-01B:** Real freighter build from consumed materials through worker/power stages; one exact new ship entity with finite hold, known ID and no created NPCs; crash injections across each SQLite/filesystem boundary.
- **SHIP-01C:** Assign actual crew, commission that freighter, then run the PLAY-13D.4G nutrient delivery entirely with player-manufactured hulls. Verify same physical resources, transit, partial shortages and restart.
- **SHIP-01D:** Colony B places a build commission at Colony A's shipyard; A accepts; production completes; hull is delivered physically; ownership and payment settle exactly once; forged orders/receipts and cross-owner privilege escalations rejected.
- **SHIP-01E:** Delegated NPC scheduling and budget-limited acceptance/expansion recommendation; a second yard exists only after materials, labor and time are actually spent. Multiple yard queues must not double-book worker IDs or materials.
- **SHIP-01F:** Linux/Windows native, SQLite restart/rollback, live host/UI, single-player interactive acceptance; documentation ledger updated with exact PR/SHA and actual pass/fail evidence. No pending/queued job is a verified pass.

## Scope boundaries and architectural rules

Use the authoritative Simulation, existing manufacturing/refining and durable SQLite journal, local work board, universal transport contracts and NPC registry; **do not** build a parallel economy, shipping or crew database. Finite materials, construction time and individual NPC work must remain causal. Avoid a browser-only solution; viewer is a temporary test UI for a native desktop game. No on-screen planetary landing is required for the player.

**Current scope checkpoint:** No shipyard, buildable dedicated freight hull, intercolony ship sales or NPC self-expansion are represented as implemented here. This document preserves approved future behavior separately from PLAY-13D.4G's unverified code.
` gave **3/3 PASS**. The new preflight test also passed a standalone `-fsanitize=address,undefined` build against the old kernel. These are **v66 compatibility and SHIP-01A.0 results only**, not native acceptance of stacked freight PR #50 at the latest v89 SHA. No exact-head v89 checkout existed in the local archive. The authoritative final SHIP-01A integration gate stays open.

## Acceptance sequence — not yet verified

- **SHIP-01A:** Real player-funded orbital shipyard construction and persistence on a chosen colony; reject unauthorized, underfunded and duplicate builds.
- **SHIP-01B:** Real freighter build from consumed materials through worker/power stages; one exact new ship entity with finite hold, known ID and no created NPCs; crash injections across each SQLite/filesystem boundary.
- **SHIP-01C:** Assign actual crew, commission that freighter, then run the PLAY-13D.4G nutrient delivery entirely with player-manufactured hulls. Verify same physical resources, transit, partial shortages and restart.
- **SHIP-01D:** Colony B places a build commission at Colony A's shipyard; A accepts; production completes; hull is delivered physically; ownership and payment settle exactly once; forged orders/receipts and cross-owner privilege escalations rejected.
- **SHIP-01E:** Delegated NPC scheduling and budget-limited acceptance/expansion recommendation; a second yard exists only after materials, labor and time are actually spent. Multiple yard queues must not double-book worker IDs or materials.
- **SHIP-01F:** Linux/Windows native, SQLite restart/rollback, live host/UI, single-player interactive acceptance; documentation ledger updated with exact PR/SHA and actual pass/fail evidence. No pending/queued job is a verified pass.

## Scope boundaries and architectural rules

Use the authoritative Simulation, existing manufacturing/refining and durable SQLite journal, local work board, universal transport contracts and NPC registry; **do not** build a parallel economy, shipping or crew database. Finite materials, construction time and individual NPC work must remain causal. Avoid a browser-only solution; viewer is a temporary test UI for a native desktop game. No on-screen planetary landing is required for the player.

**Current scope checkpoint:** No shipyard, buildable dedicated freight hull, intercolony ship sales or NPC self-expansion are represented as implemented here. This document preserves approved future behavior separately from PLAY-13D.4G's unverified code.
