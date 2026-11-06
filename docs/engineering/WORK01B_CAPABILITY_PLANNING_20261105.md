# WORK-01B.1 — Typed Work Capabilities and Canonical NPC Work Planner

**Status:** Implemented in a stacked draft branch on WORK-01A (v94). Exact-head native Windows/Linux acceptance is required; no green CI claim before jobs finish.

## Architecture: reuse original authority, no rewrites

This phase adds `include/galaxy/WorkPlanning.hpp`, a pure read-only planner over durable `Simulation` records. It is not a second job database, resource ledger, agent simulation, or owner authority. `Simulation::process_universal_work` still invokes only the original, exact authorization and resource-consuming methods for physical shipbuilding.

### Five typed capability entries

| Original source type | Typed input | Real result proof | Named workers | Autonomous executor |
| --- | --- | --- | --- | --- |
| LocalDelivery | ExistingLocalDelivery | LocalDeliveryReceipt | 1 | No (existing native delivery system only) |
| IntercolonyFreight | ExistingFreightLeg | SealedFreightArrivalReceipt | 0 (assigned vessel) | No |
| ShipyardConstruction | ExistingShipyardConstruction | OperationalShipyardReceipt | 2 | No (original construction advances natively) |
| FreighterBuild | ExistingFreighterBuild | ManufacturedHullReceipt | 2 | No (original construction advances natively) |
| CustomerShipOrder | AuthorizedCustomerContract | CustomerHullAndCommissionReceipt | 2 for commissioned hull | **Yes, only with explicit buyer delegation** |

The C++ `RequestSchema` and `ResultSchema` enums give each adapter an explicit type rather than a magic command string. Entries marked no-executor remain read-only from this coordinator. A capability declaration does not grant rights to act.

### Deterministic dependency graph and priority

`work::plan` accepts only const maps of already durable `WorkDirective`, `ShipyardCustomerOrder`, and `NpcRecord` plus the current simulation tick. It produces `PlannedDirective` values without writing to any record.

- Dependency paths follow existing same-owner, same-colony, strictly earlier directive IDs. Cyclic, missing, cancelled, or unfinished prerequisites cannot be treated as fulfilled. Original v94 load validation still rejects forged dependency receipts.
- Work already physically delivered/ready for commissioning precedes funded work, then accepted contracts, then supplier-unapproved requests. Within each stage, age (bounded to 999 ticks), posting time, and stable directive ID ensure deterministic priority. No fixed global eight-call/worker cap.
- Supplier consent remains independent. A buyer's consent never becomes supplier acceptance.
- The scheduler makes one read-only plan per canonical simulation tick. A cancelled/completed directive is not schedulable. This is not a persistent parallel queue.

### Physical worker selection and reservation

- Only a *delivered* hull ready for named-NPC commissioning requests per-tick reservations; a pending twelve-tick hull build never locks crew in advance.
- The planner checks original NPC existence, home ship, physical location, ship assignment, role and current transport/mentorship custody. Missing/unavailable NPCs are a blocked candidate, not replacement characters.
- A named NPC cannot be selected twice across READY coordinator directives in the same native tick. Higher stage/age wins; lower priority is blocked and can retry on a later tick.
- These are **derived ephemeral selection locks**, not a new inventory or workforce escrow. The original `commission_shipyard_freighter` performs final physical checks, revalidates stocks and permissions, and alone transfers real NPCs and matter.
- The old v94 `WorkBlock::NamedCrewAndSupplies` communicates unavailable/reserved workers; no snapshot-format change is needed. In a future UI phase, separate planner wait reasons can be displayed.

### Gate and verification

`tests/WORK01B_WorkPlannerTests.cpp` checks all five schema pairs; confirms no autonomous executor outside authorized customer ship orders; validates stage priority and aging, fulfilled/unfinished dependencies, per-tick competing NPC reservations, release after cancellation, transport/location unavailability, foreign contract custody and supplier consent.

The dedicated `.github/workflows/work01b-work-planner.yml` builds on Windows and Linux and runs WORK-01B, WORK-01A, SHIP-01F and SHIP-01B.2 native acceptance with the original C++/SQLite simulation. WORK-01A already covers crash injection, conservation, original shipyard and actual buyer commissioning; those remain mandatory regressions.

## Still open for later WORK-01B / WORK-01C

- Source-specific **workforce qualifications** (skill/licensing/experience) beyond real current role/custody checks, plus actual per-site machine-slot/yard queues.
- Configurable player-approved priority settings and emergency/life-support precedence with a durable authorization policy. The current stage/age ordering is deterministic but **not full global fairness or emergency arbitration**.
- General execution adapters for local shipping, freight, mining, farming, crafting, maintenance and repair—only existing customer-ship contract execution is admitted to universal delegated automation.
- Owner-scoped UI display of individual planner gate reasons, multi-owner browser sessions, and real installed-desktop acceptance.
- Exact-head Windows/Linux CI and complete SQLite recovery verdicts.

**Boundary:** This phase must not be described as automatic NPC performance of all five categories; the capability registry is not an executor registry of implemented actions.
