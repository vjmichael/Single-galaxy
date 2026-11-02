# DEMOG-05q6e3a/b — Paired family travel, child rescue and ownership fences (v43)

**Date:** 2026-11-02  
**Project:** Original Single-Galaxy C++17/SQLite simulation, not the EVEmu server.  
**Baseline:** Local DEMOG-05q6e2 v42 dependent ferry custody.  
**Status:** **Partially implemented and verified in a focused local source package. NOT merged into main game kernel, desktop client, or Windows build.** GitHub is repository/documentation only.

## Changes

1. **Both parents physically travel.** Optional co_guardian_id records a second real parent on each child's ferry custody receipt. Boarding/arrival preflight and committed restore check that both adults appear in the actual authenticated ferry command, are the child's actual ART02 parents and share the same physical ferry and voyage. These children still are cohort identities, not invented worker NPCs. Older v42 solo-parent receipts remain readable.
2. **Nonfatal child evacuation.** Host-only submit_host_dependent_rescue_action and step_with_dependent_rescue execute a real HostEvacuatePassengers action and atomically terminate the original minor leg as an emergency transfer and create a new receiving-ferry leg with a distinct transport ID. Each minor retains stable ART02 identity, home colony and parents. Receiving-ferry child berths count against the same physical 16-passenger limit as adults. New receiving ferry can subsequently disembark its real manifest; failed write/retry does not duplicate children.
3. **Durable linked provenance.** Restore ties each evacuation to original and receiving ferry, actual applied world command, new voyage, parent manifest, generations, ticks and batch counts. A child-custody activation marker and independent receipts detect deletion or alteration of surviving ledger records. Does not claim externally tamper-proof sealing.
4. **Cold/detailed ownership fencing.** RegionalModeAuthority::prepare and commit refuse any active minor berths within the same SQLite database under BEGIN IMMEDIATE, including a child on a rescue ferry. Paired-parent boarding/arrival/rescue checks stable Cold owner generation again inside the same world transaction. A test confirms an actual persisted cold→detailed→cold ownership-record roundtrip after all children disembark. The guard is intentionally coarse: in this focused package it also blocks unrelated regions when any dependent is physically in flight.
5. **Fatalities fail closed.** The child-aware host rejects valid adult-world HostDestroyPassengerFerry or HostResolveMixedFerryLoss while a physical dependent berth is occupied. This is **NOT** a completed child casualty system, but prevents a death or rescue decision from silently deleting unrepresented children.

## Verified local evidence

| Test | Result |
|---|---|
| Strict GCC 14.2 C++17 Release (-Wall -Wextra -Wpedantic -Werror, -O1) | **24/24 CTest PASS** |
| Debug + UndefinedBehaviorSanitizer (-fsanitize=undefined -fno-sanitize-recover=undefined) | **24/24 CTest PASS** |
| Inherited v34–v42 regressions | **22/22 PASS** |
| Independently extracted final-source clean CMake build + both new tests | **2/2 PASS** |
| Final packaged ZIP internal SHA-256 entries | **149/149 matched** |

New demog05q6e3a_paired_family_handoff test: two registered parents and two children; false co-parent and stale owner refused, boarding commit rollback, stranding and resume, failed in-flight tick/restart, arrival, blocked handoff during active custody, persisted cold/detailed/cold owner transition after arrival. New demog05q6e3b_dependent_rescue test: real child/parents and full adult fatal-manifest fixture, generic fatal and mixed-fatal refusal, actual capacity-checked emergency evacuation, failed SQLite commit/retry, original and rescue voyage chain, actual final disembarkation, independent recover_copy, and forged/missing rescue receipts or deleted travel ledger rejected.

## Package

`SingleGalaxy_DEMOG05q6e3ab_Paired_Parents_Rescue_v43.zip` — **474,014 bytes**, complete focused source, v42→v43 patch, CMake, 24 native tests, report, explicit child-casualty contract and local/clean-build evidence.

SHA-256: `12e4c772c717cec3a0aa1cdd5fa29e0afd7d37e2481688499eff2e0ec094470a`.

## Outstanding gates — do NOT mark full 05q6e3 complete

- **Child casualties and mixed survival:** Current code intentionally refuses terminal ferry destruction when minors are aboard. Need explicit child survivor/deceased manifest, immutable family death records, guardian/orphan disposition, family and regional population reconciliation and journaled rollback/replay. Acceptance contract inside package: `docs/CHILD_CASUALTY_ACCEPTANCE_GATE_2026-11-02.md`.
- **Atomic two-parent legal/home migration:** both adults can physically embark/rescue/disembark together, but v41/v42 society migration can administratively settle only one adult per command. Whole-family simultaneous transfer of both parents and dependents is still unimplemented.
- **Detailed regional runtime ownership:** real ownership record handoff was tested, NOT actual promoted industry and workforce simulation state replay/demotion. Need stage/reconcile and region-scoped custody gates instead of today's global conservative guard.
- **Full production integration:** no main-source merge, no client/Windows verification, no human/child treatment or institutional public safety service integration. Police/fire/complete EMS remain deferred by user instruction.

Next: explicit fatal child custody and family-death settlement with partitioned rescue outcomes; paired-parent home migration; cold/detailed live regional ownership transition and population checkpoint reconciliation.