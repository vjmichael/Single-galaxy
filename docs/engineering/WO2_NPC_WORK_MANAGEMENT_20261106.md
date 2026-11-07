# WO2 — NPC work and management (owner-directed second work order)
**Started 2026-11-06.** Stacked on [WO1 industry catalog draft PR #87](https://github.com/vjmichael/Single-galaxy/pull/87). No new player gameplay, economy currencies, planetary settlement rules, 3D assets or multiplayer.

## Engineering scope
1. Map **every WO1 production capability** (including loss-bearing recovery) and **every WO1 service role** onto explicit NPC trades/professions, required station/facility, power, staffing, skill families, experience/training path and supervising job where appropriate. This is a dictionary of *potential work*; item prototypes must not be interpreted as already operational factories.
2. Preserve original NPC identity, actual location, home-colony authority, illness/medical duty, transit/flight occupation, existing shipyard assignments, existing workshop staff, and real utility/power/material capacity. **Never spawn a worker or issue an independent parallel work receipt.**
3. Introduce an **auditable, deterministic read-only industrial workforce planner** for the two real original workshop operations: panel forming and frame finishing. It may recommend an existing available named NPC, or explain why the site is blocked; never imply a staffing order was executed.
4. Produce an **explicit owner-approved original `Command` admission envelope**, using `AssignWorkshopOperator` or `AssignWorkshopFinisher` through the original `PersistentWorld::submit` and commit tick. Host authenticates owner; a viewer request or absence of consent cannot become an approved order.
5. Protect exclusive named worker slots against conflicts with already occupied workshop stations, native shipyard build crews, active medical shifts, travel and other native duty, without breaking old-save semantics. No blanket claims of actual qualifications for professions whose educational infrastructure is not yet built.
6. Reuse original workshop power, feed, finisher installation, backpressure, work-in-progress and SQLite journal for actual production/restart evidence. New skill/management data is **capability configuration**, not independently mutable NPC biographies.
7. Define scalable staff/shift, mentorship and manager capacity **contracts** for later work, but do not fake per-person XP or work histories from generic factory batch totals.

## Exit gates
- Every WO1 site capability / production recipe / loss-bearing recovery and every service staff role has exactly one mapped profession profile; schema tests reject gaps and invalid privileged/native bindings.
- Deterministic two-slot original-workshop staffing recommendations, graceful no-staff, no-power, no-input, unbuilt-finisher and worker-taken cases; no cross-owner or offline/oracle assignment.
- Explicit owner-authenticated command into original world with durable SQLite admission/effect and recover-copy; repeat/replay cannot duplicate a worker or create production.
- Native C++ Windows/Linux compile and original worker/industry/SQLite CTest; source-generated schema parity; no security/legacy regressions.
- If source code is complete but CI is still queued, label source complete and native acceptance **pending**, never claim tests passed.

**Scope boundaries:** WO3 will decide which planet facilities can be constructed and kept supplied; WO4/5 will distribute and physically deliver their goods; WO7 society will interpret NPC preferences/household outcomes. Work definitions make no automatic colony-owner decisions.
