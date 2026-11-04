# COLONY-AUTO-04D.4D2C — Mixed guardians, cold/detailed owner races, and live tamper rejection

**Stage:** v77 follow-up to v76's real atomic world/ART02/ECON03 family evacuation.

## Problem addressed

A simple two-adult/one-child evacuation is not enough to prove that the physical custody closure works when several dependent children have distinct legal escorts. A persisted "complete evacuation" query also cannot simply trust its immutable-looking header: if a privileged/offline writer damages the member table, it must refuse the assertion even while a previously loaded in-memory simulation still looks correct.

## Concrete changes

1. `src/ColonyFamilyDeparture.cpp`: preparation cross-checks any already-registered `Simulation::PhysicalDependent` guardian against the legal and requested guardian. It now rejects silently substituting another lawful parent in the prepared manifest. This preserves the v74 ability to prepare an intended rescue *before* the child is physically registered; the v76 execution gate still requires an actual physical registered dependent.
2. `src/Persistence.cpp`: `verified_atomic_family_departure` does not issue a positive assertion merely because a matching root row claims the right counts. Before answering, it independently calls the canonical `Store::restore` audit, checks the persisted generation and canonical world state hash against the live state, then reruns immutable child registration provenance verification. Thus a removed member row, changed child guardian, falsified world snapshot, or missing regional receipt is a hard failure, not a stale "evacuated" confirmation.
3. New independent test `tests/COLONY_AUTO04D4D2C_MixedGuardiansFaultMatrixTests.cpp`: establish three *actual ART02 births* and physical child registrations. Children 1 and 3 are registered to parent 1; child 2 is registered to parent 2. Test requires all three source children and both source adults to move to a real stocked receiver hosting two adults. The expected recipient population is seven, but native worker count stays four.
4. The test prepares a real `RegionalModeAuthority::prepare` detailed-mode handoff **after** the evacuation command was queued; execution must refuse to cross the prepared ownership boundary. `RegionalModeAuthority::abort` increments owner generation and the original token must remain stale. An explicit fresh owner generation must be supplied to a new authorized execution.
5. A duplicate ART02 member event ID, a forged guardian, and a missing child are each refused. An injected `FailPoint::AfterSnapshotWrite` must roll back all three SQLite authorities, including all three family residences. A successful transfer must survive restart, independent `recover_copy`, and subsequent life-support consumption of all seven people.
6. On an independently recovered copy, the test deliberately removes one child's immutable member row after dropping its delete trigger. A **live already-open** evacuation query must detect the damage; a new restart must fail closed. This test models privileged/offline corruption, not ordinary application access.

## Invariants

A valid departure conserves every person, guardian, original birth proof, material, and regional population. No tool or player may elide a child. Family departure requires an urgent shortage, a reachable receiver, real supplies and owner authority. The pending handoff acts as a whole-region population transaction fence, not a request to pause or ignore the SQLite writer.

## Supported boundary and future tasks

This is **parent-guardian multi-child** acceptance; it does not by itself prove a new legal non-parent guardianship adjudication, cross-region escort change, chained multi-hop evacuation, or full production scheduling over multiple simultaneous colonies. Those need distinct fixtures and authorization. Work should return to autonomous colony response and recovery after this custody fault-matrix gate, rather than adding unbounded family-specific complexity.

## Validation

CI runs the v77 and v76 real SQLite native CTests together in `.github/workflows/colony-auto04d4d2c.yml`, with real engine execution (no mock SQLite). Record GitHub Actions run and outcome after completion.
