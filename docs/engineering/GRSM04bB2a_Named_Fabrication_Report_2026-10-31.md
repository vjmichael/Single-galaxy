# Single Galaxy — GRSM-04b.B2a: Named NPC Fabrication Bound to Real Material Effects

**Date:** 2026-10-31  
**Status:** Locally implemented, complete Debug C++ build passed; focused and bounded native/Node/Python tests passed. **Not the full ECON-02B workshop integration.**  
**Baseline:** ECON-02A workshop prototype and GRSM-04b.B1 named mining receipts.  
**Working model:** Linux local development, original single-galaxy repository; GitHub is repository storage/documentation, not the development hub.

## Scope and contract

This is the first step of GRSM-04b.B2, adding physically executed **named shipboard fabrication** to the existing named mining route. It reuses actual authoritative `Simulation` actions that already debit real local stocks and credit modules. It does not mint new materials, workers, credit, or reputation.

- Host-only API `PersistentWorld::submit_npc_fabrication_work(npc_id, ship, action, material_code, quantity, command_id, fail)`.
- Allowed actions: `FabricateModule` (two units of shipboard refined stock per module), `FabricateMaterialModule` (2000 kg named usable good per module; material code 1 structural alloy or 2 aluminum).
- The job requires a real, living NPC record aboard the *same* owner-controlled colony ship, `NpcRole::Ship` and current location `ship`; host records the owner's player ID as explicit issuer, separate from NPC executor identity.
- Worker may have **one named mining or fabrication assignment per tick**. Another NPC may work during that tick. Invalid role, absence, foreign ship, duplicate command, unsupported action, invalid quantity and invalid material code fail closed.
- An accepted job stores a digest-chained immutable *assignment* receipt in the same SQLite transaction as the authoritative accepted command.
- Only after `PersistentWorld::step` applies the actual command, writes an unambiguous conserved-material ledger movement and its physical CargoMovement incident, can the same transaction mint the distinct named *execution* receipt. Failed work, merely being aboard, and ordinary non-named player commands cannot create named executor credit.
- `verified_npc_fabrication_execution`, `recent_npc_fabrication_assignments`, and `recent_npc_fabrication_executions` are read-only public queries. These receipts assert observed engine execution and provenance, not NPC motive, legal blame, skill mastery, or an eyewitness report.
- Transaction failpoint rollback, checkpoint/recovery-copy, restart, and explicit tampering with worker identity, issuer, amount, input selection, event link, deletion, metadata tip, and metadata count are tested.
- SQLite schema migrated **v16 -> v17** with new bounded immutable assignment/execution chains. Old commands are **not** credited retroactively. Older migration fixtures were updated to remove v17-only tables when constructing earlier-schema test databases.

## Real production paths tested

1. Ordinary `Mine` (ten tonnes) -> `RefineOre` (ten units) -> worker-assigned `FabricateModule` (two modules from four refined units). The world retains the exact original conserved resource total; a second NPC can simultaneously do named mining. An unassigned fabrication command makes a real module but **does not** grant named credit.
2. Silicate geological asteroid -> thirty tonnes of mineral fractions -> 12,000 kg of plagioclase -> 2,400 kg usable aluminum -> a **named** material-fabrication command consumes 2,000 kg aluminum and produces one two-tonne construction module. The remaining 400 kg stays in that ship's stock; total world mass is unchanged. Both restarted SQLite state and execution receipt are verified.
3. Assigned but unfundable jobs remain assignments with no named execution. Artificial database alterations are rejected during restore.

## Files changed

- `include/galaxy/Persistence.hpp`: typed named-fabrication APIs and distinct assignment/execution records.
- `src/Persistence.cpp`: v17 migration; named work admission, transactionally journaled receipts, immutable read APIs, restore consistency checks, cross-job worker occupancy.
- `tests/GRSM04bFabricationTests.cpp` (new): physical production, authorization, one-job constraint, failure, rollback, migration, restore, recovery, deliberate falsification.
- `tests/*` legacy schema migration fixtures: downgrade cleanup and v17 current-version expectation.
- `CMakeLists.txt`: new dedicated `galaxy_grsm04b_fabrication_tests` executable/CTest registration.

## Verified local results

- Linux CMake Debug **all-target build: PASS**.
- New named fabrication test: **PASS** (real material paths, restart, recovery and tamper scenarios).
- Existing named mining and persistence tests: **PASS**.
- Bounded non-live native CTest: **43/43 PASS**.
- `node --test tests/*NodeTests.js`: **84/84 PASS**.
- `python3 -m unittest discover -s tests/design -p '*test*.py'`: **3/3 PASS**.
- Broad whole-suite attempt: **NOT COMPLETE**. CTest was interrupted while waiting for older live simulation tests; an earlier failing obsolete test asserted schema v16, was updated for v17, and the affected regression test then passed. A complete whole-suite pass is **not** claimed.
- No Windows-native game, UI, online multiplayer or extended population benchmark was performed.

## Next critical gates

**ECON-02B / B2b: real player-built outpost workshop.** Build/deploy a workshop from finite construction materials at an actual outpost; reserve and physically deliver aluminum from the colony ship to that outpost; persist itemized stocks in the authoritative world snapshot and conservation checks; require local power and present named labor; apply a bounded universal-workshop recipe in an authoritative world tick; journal input/output movements and worker assignment versus execution separately. The current ECON-02A example workshop remains an in-memory bounded prototype; it is **not** yet promoted to a live persistent facility.

**ECON-02C onward:** durable multi-tick manufacturing orders, one active worker across all professions, worker physical transit, combined multi-proficiency advanced recipes and actual skill learning, actual tools/power budgets, shortage-proposal approvals and work-board persistence, logistics, delivery/trade receipts, and eventually NPC observations and contested social interpretation. No currency has been minted; `Chits` remains a provisional name only.

## Authority and design boundaries

- The user-selected goal remains an original, single-galaxy game, initially colony ship + finite NPCs + material conservation + exploration/mining/fabrication/outpost loop, not an EVE copy.
- An assigned job is not proof of successful execution. An executed resource movement is not evidence of criminal culpability, motive, a witness's direct perception, or population-wide reputation.
- The current ship has built-in bootstrap processing. **It does not prove the ECON-02A configurable workshop has been wired to local outpost inventory.** No inflationary market, payroll, AI labor autonomy, or free workforce is introduced.
