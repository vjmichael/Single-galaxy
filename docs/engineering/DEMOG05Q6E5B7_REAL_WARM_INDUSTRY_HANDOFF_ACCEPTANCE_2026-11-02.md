# DEMOG-05q6e5b7 — Real warm/detailed region handoff acceptance (OPEN)

**Date:** 2026-11-02. **Status: Acceptance contract, NOT an implemented industrial runtime.**

The local v49 focused kernel verifies shared ART02/ECON03 population census and a cold/detailed **owner-record** round-trip. That does **not** prove an actual detailed-world industry and workforce handoff. This gate remains open until the production simulation is wired to the authoritative source.

## Required executable acceptance

1. Introduce a canonical region-scoped authority for each resident and employer assignment; bind original ferry crew to a host region and distinguish **registered residents**, **physically present workers**, **in-transit personnel** and **on-shift staff**. No duplicate enrollment or free workers.
2. Define versioned, lossless cold production checkpoints for physical inventory, machines/recipes/batches, utilities, labor allocations, maintenance and pending inputs/outputs, including exact provenance of in-progress effects.
3. Promote Cold→Detailed under a regional owner-generation compare-and-swap fence and one durable transition journal: stage snapshot, verify regional family census, material/worker conservation, and publish exactly one detailed owner. Preserve planned activity; no double-applied production.
4. Test actual detailed ticks with shipping, machine progress, workforce reassignment and resource consumption; demote Detailed→Cold with checkpoint/delta reconciliation and a durable cutover. Resume from journal after a crash before/after each write.
5. Run multi-colony tests where a ferry rescue moves physical crew while registered residence remains constant; ownership handoff cannot duplicate a worker or assign absent rescuers to productive shifts. Verify that some other unrelated region can continue operating.
6. Compare cold continuous/aggregate ticks with detailed simulation at the same authoritative time for deterministic conservation and agreed numerical tolerances; test restart, independent genesis recovery, stale/forged regional owner generations, incomplete manifest, missing factory inventory and conflicting effects.
7. Only then claim actual warm/detailed industry acceptance, followed by native Windows, full game kernel merge and client tests.

**Policy:** Child fatalities remain blocked and children must not be visually depicted harmed or dying. Police/fire/full EMS institutions remain deferred.

**Related tested prerequisite:** `DEMOG05Q6E5B6_NATIVE_CREW_REGION_PREFLIGHT_v49_2026-11-02.md`.
