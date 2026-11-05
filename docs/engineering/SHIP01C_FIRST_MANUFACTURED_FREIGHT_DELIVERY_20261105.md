# SHIP-01C — First genuinely manufactured freighter delivery (2026-11-05)

**Status: actual C++ end-to-end test source implemented on stacked DRAFT branch; exact-head Linux/Windows execution NOT YET VERIFIED.**

Dependencies: SHIP-01B.2 (native v92 commissioning, PR #54), SHIP-01B.1 (first fabricated uncrewed hull, PR #53), SHIP-01A.1 (material-backed NPC shipyard, PR #52), PLAY-13D.4G (universal physical freight, PR #50).

## The decisive missing proof

The earlier PLAY-13D.4G E2E test exercised a **separately pre-seeded crewed colony ship** as its cargo carrier. This proved the freight transport model but not whether a player could construct and operate a new carrier. SHIP-01B.1 now creates a true uncommissioned freighter entity and SHIP-01B.2 transfers existing operating crew, paid scanner equipment, and finite supplies to it. SHIP-01C uses **exactly that newly minted entity ID**, never a pre-seeded spare carrier, for the same physical Planet A → Colony B test.

## New native acceptance run

`tests/PLAY13D_RegionalFacilitiesTests.cpp` has a `--manufactured-only` mode registered as `galaxy_ship01c_manufactured_freight` in CTest. The original farm and extraction tests remain unchanged when invoked without the flag; the dedicated mode is an independent SQLite fixture.

The manufactured mode:
1. Seeds **one founding colony ship (10 existing named crew)**, its physically finite asteroid and a separate 3-person Colony B receiving ship. **It does not seed a freighter.** Genesis stocks are finite and provide conservation baseline.
2. Uses ordinary issued mining/refining/fabrication commands for the first six modules. Builds and finishes one real player-controlled yard from four modules and two named builders over eight ticks. Repeats real ore→material→module actions six more batches.
3. Queues a six-module freighter at the operational yard. It cannot queue another competing hull on the occupied slip. Advances twelve real work ticks and retrieves the **unique launched ship entity ID**, 100 m³ capacity, with no free NPC or fitted scanner.
4. Checks that this uncommissioned hull cannot initiate the celestial voyage. At the source berth the owner commissions it by moving two named existing NPCs from the founding colony, exactly one already fabricated scanner module and 250 kg each of actual food, water and oxygen. Checks durable v92 round trip, unchanged total NPCs and total world matter.
5. Takes the source colony and **this manufactured ship** through the existing 4-tick journaled physical celestial transit to Planet A, while Colony B navigates into another actual body frame.
6. Runs the existing full planetary site/spaceport/ShuttleCargo/NPC pilot tests and extracts a finite **Planet A resource** at the actual physical site. NPC work-board tasks pick up resources, fly them on a material-backed shuttle to orbit and deliver stock to the source colony.
7. Loads a sealed 200 kg nutrient manifest from the source into **the actual manufactured freighter**, verifies exactly-once cargo debit, custody, manifest identity, wrong-owner and missing/duplicate receipt rejections. Takes the carrier through its own genuine celestial transit; validates no in-flight delivery; then commits to the distinct Colony B inventory only after a matching arrival receipt and physical berth.
8. Returns the same hull for a second single-instruction delivery of 100 kg. The existing native scheduler completes a planetary pickup and automatically loads only after actual shuttle delivery, then the freighter repeats transit and unloads. A SQLite restart verifies final ship identity, no duplicated inventory or NPC, completed delivery IDs/receipts and unchanged total matter. The same built hull then submits ordinary owner-accepted yaw/thrust commands and must physically change heading and velocity/position without changing the source colony's actor ID.

This reruns the **same legacy full freight test**, swapping only the carrier's provenance and funding path. No alternate fake ship economy, separate stock table, teleported supplies, or mocked renderer has been introduced.

## Exact-head build and gate

- CMake dedicated test name `galaxy_ship01c_manufactured_freight`, a separately addressable 300-second CTest.
- `.github/workflows/ship01c-manufactured-freight.yml` configures **Linux and Windows** with native SQLite, builds the SHIP-01B/01B.2 test binaries plus the full regional facilities test, and runs those three tests against the **same PR head**.
- The SHIP-01C workflow additionally builds the real native host on Linux and executes the strict Python HTTP relay session/action matrix and the JS browser receipt-decoder suite. The preceding SHIP-01B.2 workflow covers the same backend on its own exact head.
- No green status, shipped build or end-to-end runtime completion is claimed before the actual exact-commit CI jobs have passed.

## Real player pilot controls — native host, strict relay, temporary cockpit

The host originally hard-wired ordinary movement buttons to the founding colony ship. That would leave a constructed freighter operable only through internal test APIs. SHIP-01C now adds an independent owner-authenticated **specific manufactured hull** route, keyed by its original immutable freighter build order:

- Native `FREIGHTER_PILOT_STATUS|order` publishes actual hull ID, orientation, physical local-space position, destination frame anchor, crew count and remaining three life-support stocks from the authoritative world (plus the committed generation).
- Native `FREIGHTER_THRUST|order|x|y`, `FREIGHTER_YAW|order|±15` and `FREIGHTER_TRAVEL|order|body` admit actions only if the corresponding manufacturing record belongs to the authenticated player **and founding colony**, has been commissioned, points to a living real ship entity with its immutable manufactured ID, named operating crew, real life-support stock and fitted scanner. These call the same normal `Simulation::submit` and `PersistentWorld::begin_celestial_transit` entrypoints as all other ships.
- HTTP `colonyFreighterPilotStatus`, `colonyFreighterThrust`, `colonyFreighterYaw` and `colonyFreighterTravel` require exact typed IDs and bounded input vectors/deltas/destinations. Boolean numbers, extras and forged tokens are denied.
- The temporary browser has explicit thrust, yaw, physical body travel and inspection controls under the shipyard panel, with native `FREIGHTER_PILOT_ITEM`, `FREIGHTER_PILOT_QUEUED`, and `FREIGHTER_TRANSIT_STARTED` telemetry. **Queueing a command is not claimed to have completed a world tick.**
- This keeps colony factory control and planetary work board owned by the founding colony ship. It does not silently switch those commands to an unstaffed freighter.

The long-duration test transfers a **250 kg reserve of each food, water and oxygen** from the actual founding colony to the two-person freighter. It asserts no shipboard supply-shortage cycles over the entire extraction and two physical deliveries. No infinite or secretly replenished life support is introduced.

## Next operational extensions, after acceptance

- A fleet job board that lists commissioned hulls and offers dispatch/control by selected actual hull ID.
- Authorized NPC pilot scheduling, ship repair and loss handling for manufactured hulls. Existing crew transfer is a commissioning custody transaction, **not** an independent NPC flight AI scheduler.
- Owner-to-owner shipyard commissions, ship handover at verified berths, paid contracts, bounded NPC work queues, and expansion of additional actual shipyard slips.

**Keep the stacked PR as draft until exact-head Windows/Linux native build, CTest, restart, rollback and full physical delivery all pass.**
