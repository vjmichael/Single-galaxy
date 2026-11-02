# ECON-03c9 — Autonomous cold-region industry and eight-colony trade scenarios

**Date:** 2026-11-01. **Status:** Locally implemented and verified native C++/SQLite opt-in subsystem; 3D detailed-world integration remains open.

## Scope and assumptions

The game now has an opt-in host-owned regional cold economy, built into `galaxy_regional` and using the existing `CheckpointStore` CAS, command receipts, regional authority generation fencing, and SQLite transactional rollback. It operates real, finite physical stock rather than a synthetic statistic inserted into a test report. All eight colonies have **1,000,000 inhabitants** represented as aggregate cohorts, not 1,000,000 instantiated NPC objects. Regions A/B/C/D each have two neighboring colonies.

- Every 15 minutes of **game time** (4,500 logical ticks at a scenario-specific 5 ticks/game-second), autonomous policies schedule mining, hydroponic food production, waste reclamation, smelting and component fabrication. Work is bounded by assigned aggregate NPC labor, physical inputs, available battery charge and solar generation, and finite mineral deposits. Each committed shift is a durable host command receipt, not a per-NPC tick.
- Food production converts allocated nutrients into food. Organic waste is reclaimed into nutrients; wastewater into reusable water; CO2 into oxygen. These are **simplified, ideal 1:1 game conversions**, not an accurate chemical, agricultural, energy, or ecological model. Material cannot appear without a source. Smelting has 95% material yield and fabrication 90%, with the remainder preserved as tailings/scrap.
- Space carriers are finite named vessels with fixed origin/home, crew minimum, capacity, speed, fuel and unavailable-until-return reservations. Routes derive from stored 3D site coordinates; departures and arrival clocks are persisted. Return-leg time and fuel are reserved at dispatch. These are **macro physical routes**, not rendered ships, collision, combat, navigation AI or actual NPC crew individuals.
- Shipments use a durable global SQLite in-transit manifest and have atomic origin debit and destination credit in their **respective regional checkpoint transactions**. An arrival cannot credit a destination more than once; the in-flight cargo remains in custody until received. The host processes due arrivals in chronological order. Cross-region cargo transfers are not a single distributed two-region transaction; the in-transit custody entry is the committed bridge between the two independent transactions.
- Prototype host policy accepts one trusted fixture actor in these tests; production authentication and real crew/ownership receipts are not yet connected to `PersistentWorld`. This module is **opt-in**, and does not claim to replace the current playable-world inventory or spawn a million detailed NPCs.

## Eight-hour scenarios

At **0h A**, **1h B**, **2h C**, **3h D**, **4h C**, **5h B**, **6h A**, then remain at A through **8h**. Both scenarios consist of eight million-person colonies and 144,000 logical ticks; each visits all four pairs. The fixture uses instant camera travel; actual navigation time is not included.

| Measure | Within-pair trade | Every-colony-to-every-colony trade |
|---|---:|---:|
| Autonomous shifts | 256 | 256 |
| Freight departures | 56 | 112 |
| Freight deliveries before eight-hour cutoff | 56 | 111 |
| Freight still in flight at cutoff | 0 | 1 (ETA later than cutoff) |
| Directed cross-network origin/destination combinations | 0 | 56/56 |
| Regional ownership flips (brief read-only projection) | 14 | 14 |
| Rejected stale cold workers | 7 | 7 |
| Deliberately rejected bad/faulted commands | 4 | 4 |
| SQLite close/reopen recovery checkpoints | 3 | 3 |
| Population shortages | 0 | 0 |
| Initial vs final mass, including cargo in custody | 1,536,000,000 kg | 1,536,000,000 kg |

Each colony's finite mineral deposit decreases from 100,000,000 to 96,800,000 kg. Across both scenarios, 25.6 million kg is mined, 20.48 million kg of ore is processed into 19.456 million kg of metal and 1.024 million kg of tailings, and 15.36 million kg of metal is fabricated into 13.824 million kg of parts and 1.536 million kg of scrap. Hydroponic food output is 128 million kg, funded by initial and recovered nutrients; eight million residents consume 128 million kg per resource across sixteen half-hour needs deadlines. Water/oxygen reclamation takes from *actually consumed* waste and CO2. No shortage was recorded over the window, but eight-hour sustainability does not establish indefinite equilibrium.

**Repeatability:** independently repeated executions produced byte-for-byte identical per-hour snapshot CSV files for each scenario. Release execution is accelerated (under one second per scenario on the development host); this is **eight simulated hours**, not eight hours of wall-clock endurance. Both final scenarios passed under AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`).

## Recovery and negative tests

- Restart three times with all four regional SQLite checkpoints and the global freight ledger, requiring identical state before/after reopen.
- A carrier cannot be rebooked while its outbound/return mission is still active.
- Command authorization rejects an untrusted actor, with no partial effects.
- Inject an abort during manifest insert after origin stock/fuel have been staged: the inventory, fleet, checkpoint and command receipt all roll back.
- Inject an abort during delivery stage update after destination credit has been staged: no duplicate or partial credit remains.
- Duplicate delivery IDs deduplicate to the original receipt, preventing repeated credit.
- Old cold-owner generations cannot mutate a region while detailed ownership is held.
- Every hour checks resource conservation, population, shortages, ownership revisions, physical shipment clocks and SQLite integrity.

## Regression scope and limitations

- **Regional predecessor integration:** eight relevant tests passed, including the real `PersistentWorld` bridge and detailed-world effect seal. Neither path was modified to silently import macro production as physical detailed-world receipts.
- **Native full project:** complete build succeeded; 91/93 CTest entries passed across disjoint groups, but two preexisting B65 live-relay QA tests failed (`galaxy_b65_live_gate_qa_e2e`, `galaxy_b65_relay_qa_controls`), including isolated rerun. Do **not** describe the full native suite as green. These tests are not in the new cold industry path, but their root cause remains unresolved.
- **JavaScript:** 93/93 passed. **Python design:** 3/3 passed.
- **Sanitizers:** both scenarios passed with ASan/UBSan.

**Major open gate:** transition to warm mode uses a **read-only synthetic projection** staged under `RegionalModeAuthority` and immediately relinquished. It verifies safe ownership fencing and checkpoint preservation in the presence of real industry freight, but it does **not** run a real `PersistentWorld` with a million detailed NPCs or detailed ships for an hour. Warm industrial work, ship AI/route obstruction, independent host actor authentication, real construction/energy plants, market payment/contracts, 3D entity streaming, and a full multi-hour wall-clock soak remain future integration gates. Simulated trading here is material exchange, not a settled financial market.

## Reproduction

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_EIGHT_COLONY_SCENARIOS=ON
cmake --build build --target galaxy_econ03c9_eight_colony_scenarios -j4
./build/galaxy_econ03c9_eight_colony_scenarios paired /tmp/sg-paired
./build/galaxy_econ03c9_eight_colony_scenarios network /tmp/sg-network
```

Use source-controlled production inputs/command receipts, not hand-edited SQLite rows. GitHub is the source repository only; builds and tests are local.
