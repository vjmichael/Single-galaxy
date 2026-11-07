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
| WO2-H — Original named-NPC trade qualification and practice | **Source implemented** in the original `NpcRecord::industrial_training`, with immutable per-person, per-trade `NpcTradeTraining` credits (3 for certification), explicit owner-approved `TrainWorkshopTrade` original commands and the existing installed panel cutter/frame finisher only. A successful practice step consumes **1 kg of aluminum or panels plus one stored-energy unit**, produces 1 kg of actual workshop scrap, and creates an Applied command receipt; no workforce, people or matter are created. Version-105 original-world snapshot encode/decode, pending command recovery, immutable receipt/duplicate integrity validation, casualty identity preservation and original SQLite command decode updated. Native regression extended for real practice, skills, owners, forged/duplicate rows, queued/recovered commands and copied/normal SQLite restarts. **Native Windows/Linux CI pending**, not yet acceptance. |

### Remaining WO2 tasks

- **Extended trade training** for the 27 unbuilt production capability families and 9 service staff roles when their actual facilities, materials, qualifications and executors exist. WO2-H trains **only cut and shape** on already-installed original workshops; never invents 36 runnable systems.
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


### WO2-H — qualification authority and persistence contract

- **Canonical owner:** `Simulation::npc_records()` remains the sole living NPC identity/qualification owner. `NpcRecord::industrial_training` carries a maximum of six immutable `NpcTradeTraining` receipts, each pointing to an existing original accepted command ID, applied event tick, real workshop site and one stable native trade ID. A deceased NPC's original identity retains those training receipts; no mutable side registry, auto-created residents, generic XP grants or fabricated foreign skill licenses.
- **Original execution path:** Owner confirmation builds `Action::TrainWorkshopTrade` through `approved_original_trade_training` (cut=1; shape=2). The host authenticates the owner, then the original `PersistentWorld::submit` and actual simulation tick recheck finite named worker, physical co-location, standing NPC duty, actual installed equipment, powered local utility, finite input stock and room for scrap. Each success converts 1 kg stock into 1 kg recoverable scrap and debits 1 real energy unit; no phantom supply or mass destruction.
- **Strict credential threshold:** 3 **independent applied original-world training commands** certify the corresponding native trade. Industrial training is not inferred from farm XP, generic medics, aggregate output, active machine assignments, or from data-only professions. Existing v104/native baseline operators remain allowed to operate the two original machines pending the dedicated workforce transition—**WO2-H does not retroactively invalidate old assignments or claim all workers certified**.
- **Snapshot v105:** Existing v1–v104 snapshots continue to load unchanged. The first accepted training command writes a v105 original snapshot, and pending commands survive SQLite restart. New `npctraining` records are embedded in canonical original-world snapshot bytes and hash; rehydration rejects duplicates, invalid trade codes, excessive credits, unknown persons, missing original accepted-command IDs, bad ticks and absent/mismatched Applied original event evidence. The original SQLite immutable command/historical event journal remains authoritative for action and owner provenance. No separately writable qualification store exists.
- **Acceptance gate remains open:** native C++ CTest on Linux and Windows, exact-head SQLite reopen/copy, tamper and unchanged-material checks must pass; do **not** mark WO2-H tested merely because source/tests/workflow were committed. WO2-I actual time-bounded shift admission and WO2-J authorized supervisors are next, not quietly implemented by this change.
