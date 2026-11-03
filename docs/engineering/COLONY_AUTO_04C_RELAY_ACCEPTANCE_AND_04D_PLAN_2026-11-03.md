# COLONY-AUTO-04C — Authenticated safety UI acceptance and next physical safeguards

**Date:** 2026-11-03  
**Baseline:** COLONY-AUTO-04B v66, branch `integration/colony-auto04b-safety-host-v66-20261103`  
**Implementation branch:** `integration/colony-auto04c-relay-negative-acceptance-v67-20261103`  
**Status:** Negative-path browser relay acceptance tests added; **not a completed COLONY-AUTO-04 implementation**.

## Source of truth

`PersistentWorld` owns physical crew, occupied sites, food, water, oxygen, world ticks, and SQLite safety evidence. The browser displays telemetry and requests host actions; it is never a second writer.

Consumption continues regardless of whether local replenishment or production is disabled. Forecasts use **committed, physical stocks**; scheduled imports and speculative production never count as inventory. Critical/emergency responses cannot be delayed by a social tolerance/morale timer.

The occupied-site safety preflight refuses player orders to seal exits, disable distress, dump atmosphere, or prevent emergency repairs. Records establish **attempted orders**, not player motive, and are not automatically NPC memories or a global morality score.

## Implemented in v67

- Extend `COLONY_AUTO04B_LiveSafetyTests.py` to exercise hostile browser action names and malformed safety-site IDs using a logged-in owner session.
- Verify that commands rejected at the authenticated relay cause **zero safety records** before any valid preflight.
- Retain existing valid preflight, read-only assessment, principal checks, CSRF/origin checks, atomic writer, restart and deletion-tamper tests inherited from v66.

## Explicitly unimplemented

1. Actual authorized production stop/resume, resource-backed maintenance and repair commands applied as canonical, replayable world operations (the current production action is *preflight only*).
2. A physical self-preservation scheduler: first warning, then independent inspection/repair, request for supplies, cargo-backed mutual aid, and finally capacity- and route-verified evacuation. No teleportation, phantom fuel, free food, or NPC deletion.
3. Delayed social consequences *separate from* hard physical survival: colonists can become disillusioned, decline assignments, seek relocation, and tell eligible witnesses, but cannot be forced to remain in a lethal habitat by low dissatisfaction.
4. Named-NPC GRSM observations: bind a source event to a particular NPC's **verified presence and observation opportunity**; require evidence before a firsthand claim; propagate hearsay only along actual contact links with parent provenance. Informed NPCs may remember attempted abuse without universal omniscience.
5. Windows/browser acceptance and full native CTest/CI result on this branch are still pending.

## Next gate: COLONY-AUTO-04D physical policy and rescue

- Admit a signed owner stop/resume request through the single canonical writer, explicitly select production (never consumption), enforce idempotency, and journal generation/tick.
- Keep absolute occupied-site protections and NPC emergency authorization outside the player's ability to revoke.
- For every affected occupied compartment, recompute real-stock runway after each committed tick; enqueue *one* deterministic safety event per threshold transition.
- Repair only with physically allocated spares, working technicians, reachable utility and a genuine repair operation. Mutual aid must debit donor stock and credit receiver atomically.
- Evacuate only against verified route, available seats, functioning transport and transfer of named adults, minors, guardians and operating crew; all-or-nothing roster custody. Remain stranded (tracked) when no rescue is possible rather than disappearing.
- Verify restart/replay, cold/warm reconciliation, interruption rollback, owner forgery, and deliberate duplicate orders, alongside browser UI showing exact committed outcomes.

### Acceptance rules

- Disabling replenishment does **not** disable consumption.
- A starved occupied site cannot be made safe by a UI flag or unfulfilled import promise.
- Players can stop their own production but cannot seal emergency egress, censor distress, or forbid survival attempts.
- No NPC is evacuated twice, duplicated, silently killed, or separated from dependent custody through partial transfer.
- NPC memories follow individually grounded observations and contact history, not access to omniscient database truth.

**Verification note:** A committed test is not a passing test. The v67 test change requires GitHub CI or a real local native host run before marking this gate green.
