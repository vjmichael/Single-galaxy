# DEMOG-05q6e5b8 — Full production-network handoff acceptance (OPEN)

**Date:** 2026-11-03. **Status:** Acceptance contract, NOT a completed multi-factory feature.

The verified v50a adapter proves real material/WIP and named staff custody for a single quantified factory in one SQLite region. The next gate must extend this to all constructed player factories and prevent any industry or market path from mutating an owner's inventory behind the current cold/detailed authority.

## Required acceptance

1. Bind every factory, machine, storage node, conveyor and utility to a physical construction/entity ID, authoritative regional owner and immutable stocking provenance. Initial ore/parts must be atomically debited from real world inventory into factory escrow; unique receipts alone are not sufficient to establish original custody.
2. Persist a versioned graph-wide checkpoint: recipe configuration, WIP reservations, feed/finished/scrap inventories, conveyor/warehouse escrow, power, maintenance, worker/shift assignments and all in-flight production/input/output effects, without arbitrary factory area caps.
3. Cold/detailed promotion must stage the complete graph under one owner-generation CAS: active machine footprints and staffing must match real constructed world entities, while every batch and material is counted once. Prepared transitions freeze both simulation paths.
4. Cold and detailed processing must independently reproduce validated material and job effects under the same world schedule. Compare actual graph-wide output, warehouse/transport movements and cohort needs on identical logical timelines, not only owner flags. Include mid-batch and simultaneous delivery tests.
5. A detailed owner may continue receiving and shipping real materials through coordinated host receipts, but no detached regional command, market order, workshop worker, source-ferry rescue or production callback can bypass its effect journal.
6. Demotion must flush every machine/warehouse/shipment delta, active workers and shift states and advance the cold checkpoint atomically. Test deliberate failures after each component and verify independent replay/restart, stale generations, duplicate effect IDs and tampered inventory or roster receipts.
7. Establish authoritative source region/employer bindings for rescued ship crews, including away/on-shift/available distinctions, and verify that independent regions continue operating. Include injuries, multi-occupation proficiency and staffing shortfalls when those models are integrated.
8. Merge the validated adapter into the original game's single authoritative kernel, then run native Linux and Windows installation, persistent save/restart and actual GUI scene interaction acceptance. GitHub remains a repository, not a CI or development dependency.

**Policy:** No visual depictions of children being harmed or dying. Dependent ferry fatalities remain blocked. Police/fire/complete institutional EMS remain deferred.

**Tested prerequisite:** `DEMOG05Q6E5B7_REAL_COLD_DETAILED_FACTORY_v50a_2026-11-03.md`.