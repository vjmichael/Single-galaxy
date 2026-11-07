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

## Source implementation progress — draft PR #88

**Status: WO2 in progress, not yet completed or native accepted.** Native WO2-G coverage forecast and tests committed; do not equate a preview with actual signed shifts. Branch `industry/wo2-npc-work-management-20261106` is stacked on INDUSTRY-01 draft #87.

| Subtask | Actual source outcome |
| --- | --- |
| WO2-A — Industry-to-profession map | **29 production site capabilities** covering all 71 standard and 3 recovery recipes; **9 actual service staff roles** across 8 services; profession, skill, minimum workers, future training/supervisor and 4 workforce stages |
| WO2-B — Typed registry | Generated source-driven C++ `WO2ProfessionCatalog.generated.cpp`; strict fail-closed all-recipe and all-service coverage, never invents an original-world credential |
| WO2-C — Native two-slot workshop staffing | `WO2IndustryWorkforce.hpp` recommends **only** physically present, original existing NPCs, excludes current/future medical shifts, active other workshop slots and shipyard work; owner-approved existing `Command` only, actual execution through `PersistentWorld::submit` |
| WO2-D — Original world double-booking protection | `Simulation::apply` refuses stale/duplicate named workshop assignments and medical shifts that reuse an active workshop worker; original same-slot idempotence preserved |
| WO2-E — Industry bill-of-work | `WO2WorkPrograms.hpp` expands a requested item into real raw source requirements, industry steps, worker trades and worker time/energy; service planning identifies staffing, facility, supplies, equipment without fictitious service inventory |
| WO2-F — Source and native tests | **6/6 direct V8 profile/unit source checks passed**. `WO2IndustryWorkforceTests.cpp` and dedicated Windows/Linux native CMake/CTest/SQLite workflow committed; **real native results are pending** |
| WO2-G — Read-only shift coverage preview | **Implemented source**, `plan_provisional_workshop_coverage` inspects the two original stations for a finite future tick window (max 10,000), present/in-flight worker fitness, existing assignments, overlapping native medical shifts, duplicate workers and owner/site authority. Returns *provisional suggestions only*: no worker reservation, scheduled shift, resource allocation or manager authorization. Native regression extended; **CI acceptance pending**. |

### Remaining WO2 tasks

- Durable per-NPC trade experience/qualifications with real training receipts (existing farm/medic skills alone do not certify unrelated professions).
- **Executable**, time-bounded, restart-safe staffing shifts, coverage/relief, worker cancellation/reassignment and original-world owner-approved work policies. WO2-G adds a read-only forecast but does **not** persist or execute shifts.
- Authorized real NPC supervisor appointment, mentorship, job-board approval and accountable cancellation/reassignment, without granting managers the ability to spawn people, equipment or resources.
- Independent native Linux/Windows compile and SQLite work/recovery acceptance; repair actual failures, retain the draft until accepted.

**Boundary:** The other non-native recipe and service profiles remain job definitions until WO3 builds physical institutions and WO4/5 supply and distribution. No new player gameplay, currency, multiplayer or 3D work. WO3 has not started.


### WO2-G forecast acceptance contract

- Future shift windows must begin after the current world tick, end after start, and last **at most 10,000 original ticks**. An invalid window has no effect and cannot become an original command.
- Each row is limited to the two installed original workshop slots. The source-of-truth NPC record, home colony, physical location, current work assignments, real medical shift intervals, fitness and worker identity must be consulted; the same NPC cannot fill two rows.
- The preview is not an approval, reservation, worker entitlement, durable shift, payroll entry or manufacturing receipt. Only an authenticated owner and the original `PersistentWorld::submit` + tick may execute a supported assignment.
- Existing worker incumbents are **not displaced** by the forecast. The future solver must add owner-approved relief/cancellation and durable guard checks against changing conditions before actual shifts are released.
- Source change on [PR #88](https://github.com/vjmichael/Single-galaxy/pull/88); C++ runtime/SQLite acceptance **not yet confirmed**, and WO2 remains open.
