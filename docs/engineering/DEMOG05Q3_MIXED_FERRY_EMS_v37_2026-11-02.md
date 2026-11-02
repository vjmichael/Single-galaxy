# DEMOG-05q3 — v37 Mixed-Survival Ferry EMS Dispatch

**Date:** November 2, 2026  
**Status:** **Locally implemented and verified**, focused C++17/SQLite kernel. **Not merged into main game kernel or integrated into a medical facility or GUI.**  
**Baseline:** DEMOG-05q2 v36; original Single-Galaxy game, not EVEmu server.

## Implemented

- Host-only `HostResolveMixedFerryLoss` conducts a single **atomic** incident with both rescued survivors and durable fatalities. The command names **all persons physically aboard** and separates them into a sorted survivors group followed by a sorted fatalities group; `amount` is the survivor count.
- The real nearby rescue ship must be in the same system, crewed, and within physical transfer range. Occupied and pending ferry berths are checked against the existing 16-passenger limit.
- `Command.id` becomes the immutable rescue voyage ID, linked to the lost original voyage. The original ship becomes a persistent wreck.
- Each death produces a canonical `NpcCasualty` tombstone and decrements the deceased NPC's **home colony** crew. Surviving passengers keep stable IDs, experience, mentorship and permanent assignments.
- A read-only `ferry_emergency_report(original_voyage_id)` reconstructs incident, wreck, rescue vessel, survivor IDs and casualty IDs from canonical world state. It is not a second authority or database.
- The durable SQLite journal, receipt, physical incident facts and snapshot are updated atomically; recover-to-copy independently reproduces the exact canonical state.
- Snapshot version `galaxy-kernel-v37` is accepted with backwards support for previous versions. Old action IDs remain unchanged.

## Verification — local

- GCC 14.2 Release with `-Wall -Wextra -Wpedantic -Werror`: **13/13 CTest pass**.
- GCC 14.2 Debug with UndefinedBehaviorSanitizer: **13/13 CTest pass**.
- Fresh ZIP extraction, new Release configure/build: **13/13 CTest pass**.
- Inherited v34–v36 tests remain passing.
- New tests cover two survivors (including Bob), seven fatalities, restart, EMS disembarkation, independent genesis replay, malformed manifests, nearby-range enforcement, overbooking and simultaneous dispatch capacity. Corrupted rescue links, casualty tombstones, altered physical incident records and forged command history fail closed. Pre-commit injection leaves no partial incident and same-ID retries do not duplicate casualties.

## Explicit restrictions

This is **EMS dispatch and custody**, *not* clinical medical treatment. No medic scheduling, first-aid consumption, injury severity, treatment, medical supplies, or combat damage resolver has been implemented. To avoid corrupting home/assignment relationships, this v37 mixed-loss operation requires the **original ferry's own permanent crew to be among the casualties**; surviving external passengers retain all assignment metadata. Generalized crew rescue/rehoming, cold/warm movement, full family/workforce recomputation, client UI and native Windows tests remain open.

## Next gate — DEMOG-05q4

Physical EMS readiness, injury-versus-death outcomes, actual clinic staffing and supplies, authoritative rescued-crew reassignment, durable medical care receipts, and multi-region population reconciliation. Do not convert database restart into an arbitrary mortality event.

**Artifact supplied separately:** `SingleGalaxy_DEMOG05q3_Mixed_EMS_v37.zip` (source, tests, patch and evidence; not a repository merge).
