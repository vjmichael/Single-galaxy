# SHIP-01B.1 — First player-manufactured physical freighter (2026-11-05)

**Status: Code implemented on stacked draft branch; exact-current-head native C++/SQLite acceptance NOT yet passed.**  
**Depends on:** SHIP-01A.1 yard construction (PR #52), SHIP-01A design (PR #51), PLAY-13D.4G intercolony freight (PR #50).  
**Scope:** prove that the first physically built cargo hull can be created from actual colony materials, by an operational player-built yard and available named NPC labor, without spawning free workers, supplies or ships.

## Player instruction and real production

An authorized player uses `FREIGHTER_BUILD|yard_order_id|new_build_order_id` (strict HTTP `colonyFreighterBuild` or native command). The requested yard must be a *completed* existing `ShipyardConstruction` at the player's real colony ship. The native Simulation verifies owner, a stationary in-space source ship, physical local frame, two assigned available named yard builders, finite life support and **six real fabricated modules** (12 ore-equivalent units). The yard has exactly **one active assembly slip**. A duplicate order, missing modules, foreign yard, multiple simultaneous active ships or active parent-vessel celestial transit is rejected.

The same committed SQLite world transaction debits those six modules once and creates one `FreighterBuildOrder` with 12 units of embodied materials, a twelve-tick labor requirement and the origin system/body. There is no newly spawned ship at posting time, no substitute inventory, no free NPC or ship fuel. `Simulation::total_ore()` counts the single reserved material in the unfinished build.

Only `Simulation::step()` may advance the build while the real workers are physically at the source ship, in duty-ready roles and the colony has food, water and oxygen. A moving ship, interrupted transit, missing workers or supplies stalls the unfinished hull without inventing progress. After the twelfth committed work tick, `Simulation::add_entity` creates the **one actual authoritative ship** (`EntityType::Ship`) with a 100 m³ cargo hold, 100 hull integrity, no fitted scanner, no weapons ammunition, no crew, no life-support stocks, and a location **1 metre from the source ship inside its exact celestial frame**. The permanent build record links the minted Entity ID, and its 12 ore-equivalent units of embodied physical hull mass remain conserved. If the uncommissioned freighter is destroyed by a real authorized event, it becomes a Wreck, loses its navigable ship-frame custody, and retains the original immutable build receipt and physical embodied mass across SQLite restart rather than becoming a ghost ship.

## Uncommissioned is not a virtual freighter

The finished hull is an **actual normal ship entity**: it appears in canonical `entities()`, consumes a unique monotonic entity ID, carries real position/frame identity, and survives SQLite restarts. It is not yet a `ColonyShip` with a passenger roster or life-support inventory. No `npc_records` are added. The ordinary command submit and execution boundaries reject attempting to pilot the uncrewed manufactured hull, preventing a player order from silently becoming a free NPC. It is a physically built asset awaiting separately authorized **named NPC boarding, real provisions, module fitting, and a commissioning receipt**. It therefore cannot yet run the PLAY-13D.4G freight loop as a crewed ship.

This deliberate two-step gate avoids the previous freighter design's test shortcut, in which a separately pre-seeded existing crewed colony ship served as a carrier. The later SHIP-01B.2 commissioning must transfer existing NPC custody and resources under real transport/berth rules; only then should the universal freight adapter accept it. **Never** add a fictitious free crew merely to satisfy the original colony-ship carrier gate.

## Save integrity

Snapshot v91 writes each `FreighterBuildOrder`, exactly recording owner, original yard and source, materials, progress, posted/latest tick, minted ship ID, completion tick and original celestial frame. The minted `Entity` gains a v91 **immutable `manufactured_order_id` field**. The parser validates both links in both directions: every minted ship must have one finished manufacturing order, and every completed order must reference the matching actual ship. It rejects absent/duplicate hulls, wrong capacity and manufacturing identity, impossible timestamps, duplicate active assembly slip, unowned yard, unknown stock or missing yard provenance. Pre-v91 saves remain readable; v91 serializes one extra field on each Entity while retaining preexisting field order in C++ aggregate initialization.

The material ledger remains attached to the real build record after launch; it is **not** a second cargo inventory. Future ship recycling/scrapping must reconcile this embodied mass, not make it disappear or mint a duplicate output.

## Native and user interface

New owner-scoped host commands:
- `FREIGHTER_BUILD|yard|order`
- `FREIGHTER_STATUS`

Authoritative post-commit telemetry:
- `FREIGHTER_QUEUED|order|yard|generation`
- `FREIGHTER_ITEM|order|yard|progress|required|ship_id|generation`
- `FREIGHTER_LAUNCHED|order|ship_id|generation`
- `FREIGHTER_DONE|count`

The temporary browser exposes a freighter fabrication control under the shipyard panel and displays the **actual entity ID** only after committed completion. The strict HTTP relay rejects boolean-as-integer IDs, forged sessions, extra fields and duplicated yard/order IDs. UI progress is read-only; the browser cannot manufacture a hull by sending an event.

## Test gate (written, not exact-head native-run)

New `galaxy_ship01b_first_freighter` CTest target:
1. Seed only the founding colony ship, finite NPC crew, a single real asteroid and life support.
2. Mine, refine and fabricate four modules; commit the first shipyard, advance eight real ticks.
3. Mine, refine and fabricate another six real modules; reject a foreign, premature and duplicated freighter order.
4. Inject `FailPoint::AfterSnapshotWrite` during order creation. Confirm no material debit, no entity, no generation advance.
5. Commit the six-module debit; verify no early hull, one active slip and unchanged world-matter total.
6. Progress five work ticks, close real SQLite and reopen. Verify exact v91 snapshot and still no new ship.
7. Advance six more ticks, inject a failed twelfth-tick commit. Verify no phantom hull, no construction progress, no entity-ID advance.
8. Commit the twelfth tick and prove a **new real ship Entity ID**, 100 m³ physical cargo hold, same celestial frame and short-range position, no NPC roster, no scanner/weapon or stocks.
9. Prove the uncommissioned hull cannot submit a thrust command; verify no second minted hull on later ticks. Then inject a real authorized hostile hull-destruction event, verify the destroyed freighter is a persistent Wreck (not a second ship), its ship frame is removed and its original 12 units of embodied material remain accounted for. Reopen SQLite again and verify the exact original manufacturing ID survives as wreck provenance.
10. Reject a forged ship ID and an altered immutable entity manufacturing receipt; reopen SQLite a second time and check exact permanent ship identity and resource conservation.

The exact branch JavaScript test source for relay/telemetry/UI was executed in an isolated V8 harness with Node-style test/assert/fs shims and **21 of 21 synchronous cases passed** at the implementation snapshot. That does not demonstrate a native C++ build or the real SQLite acceptance test. The new CTest requires exact-head Linux/Windows compilation and execution before calling this gate verified.

## Still open

- Dedicated fixed **orbital assembly slips**, power budget, structural component blueprints and production throughput. This minimal first hull still uses the earlier colony-associated yard, not a fully independent, powered station.
- **SHIP-01B.2 commissioning:** transfer actual NPC crew, install real scanner/fittings, provision life support, authorize the built ship's controls and use it as PLAY-13D.4G cargo carrier. No free crew.
- **SHIP-01C:** complete Planet A pickup → this newly manufactured freighter → Colony B material delivery, proving no teleportation.
- **SHIP-01D/E:** intercolony shipbuilding purchase orders, multiple yards, delegated NPC manufacturing and demand-driven expansion.

**Do not merge or label native-verified until exact current v91 C++/SQLite build, restart, rollback and functional integration tests actually pass.**
