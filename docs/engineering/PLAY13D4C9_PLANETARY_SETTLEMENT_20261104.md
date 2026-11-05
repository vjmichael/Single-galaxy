# PLAY-13D.4C.9 — Permanent Planetary Settlement and Workforce Transfer

**Status 2026-11-04:** 4C.9A admission preflight and native CTest source committed. The authoritative population writer, persistent migration receipts, migration policy integration, GUI commands, full crash-injection matrix and exact-head native acceptance remain **open**. Do not infer settlement is functional from this gate's pure policy test.

## Current authority

4C.8 leaves surface visitors in `PlanetaryShuttleState::disembarked_ids`, with optional temporary `duty_ids`; the named `NpcRecord` still has a parent-ship permanent assignment. Shuttle flight and custody are real, but there is **no permanent surface residency mutation**. The existing `SocietyMigrationPlan`, `HostSettleMigrant`, voluntary immigration admission, physical population ledgers and family-custody checks must be reused instead of inventing a new NPC roster.

## 4C.9A — Read-only eligibility, implemented

`include/galaxy/PlanetarySettlementAdmission.hpp` defines deterministic, fail-closed `preflight_settlement_admission`, which is entirely pure and can be called by native desktop or host code once facts have been grounded in the canonical world. It rejects invalid principals/site, absent or in-flight visitors, repeated transfers, unresolved household custody, missing voluntary consent, competing assignments, and missing *finite* housing, work slots or life support. `tests/PLAY13D_PlanetarySettlementAdmissionTests.cpp` exercises the reason codes; CMake registers its native CTest.

**Security:** never accept client-provided boolean admission facts as trusted input. They must be computed under the authoritative world lock using actual site/ship ownership, physical visitor custody, individual and dependent records, immigration policy, and inventory reservations. A successful pure check is **not** an admission or an allocation.

## 4C.9B — Authoritative commit (OPEN)

Implement one host-owned `PersistentWorld` operation, e.g. `settle_planetary_visitor(owner,ship,key,npc,request_id,admission_receipt,FailPoint)`:

1. Require genuine named NPC, the exact PGF surface key and site owner, commissioned port, and physical presence in `disembarked_ids`. Reject shuttle/ferry flight and elsewhere-assigned personnel.
2. Obtain the existing voluntary immigration/settlement consent proof and family-custody disposition from the real society/migration subsystem. A silent role change is forbidden.
3. Reserve finite settlement housing, support stock and permanent work capacity; do not treat the site's existing two crew slots as unlimited. Support departure/repatriation and dependency after the transfer.
4. Make one candidate world. Apply the preflight on facts extracted **from that candidate**; consume the existing migration/settlement event exactly once; remove the NPC from disembarked and temporary duty ledgers; transfer permanent residency/assignment to the same surface region; update canonical ship and site headcounts/roles consistently. Never create another `NpcRecord`.
5. Write durable unique operation receipt and snapshot in the same SQLite transaction, then publish on commit. Rollback must leave population, occupancy, family custody, inventories and shuttle manifest unchanged.
6. Validate reload: one person, one custody, one residence, one duty; no lingering ferry claim, no orphaned dependents; replayed request returns the committed receipt without repeating migration.
7. Distinguish **resident** from **employed**. A consenting resident may have no job; full work slots must block an employment assignment, not force deportation or silently fabricate work. Future multi-role/colony hiring belongs to a distinct explicit job assignment operation.

Before implementing step 3, extend existing planetary facility capacity/life-support representation as real persistent resources and decide how resident supply differs from a temporary visitor support cadence. No unconditional `permanent_work_slot_reserved` placeholder may be used in production.

## 4C.9C — Household and rescue integration (OPEN)

Reuse the established guardian/dependent co-manifest and migration acceptance flow, not a new shuttle-specific child registry. Initially refuse permanent family immigration unless the entire co-dependent movement can be committed atomically, with all required people physically present, legal guardianship reconciled and enough seats/housing/support. Do not strand a child while transferring a guardian. A failed site admission leaves every person in their original physical custody. Emergency evacuation and settlement reversals must use explicit transport, not teleportation.

## 4C.9D — Native commands and gameplay (OPEN)

Add explicit `SETTLE` (admission) and `ASSIGN_PERMANENT_WORK` (employment) to the native desktop command service only after 4C.9B is complete. Relay/browser remains the temporary test adapter. Report residency, job assignment, household admission, per-person finite support and receipt; show resident vs visitor separately. Orbital player never walks on the planet.

## Acceptance gates

- Native tests: forbidden site/owner, no consent, occupied ferry, absent visitor, full site, no support, competing jobs, already-settled replay.
- Real migration: same NPC ID before/after; sum of physical population unchanged; source crew decremented once; site resident capacity incremented once; no duplicate/shadow workforce.
- Family matrix: guardian with dependent, nonparent guardian, two guardians, mixed household, interrupted co-manifest; require no child left behind.
- Crash matrix: fail after receipt write, snapshot write, prior to commit; independent process restart, no ghost settlers.
- Physical operations: resident remains after shuttle ascent; subsequent ship departure does not consume their food; settlement consumes finite planetary supplies; without support work pauses or lawful evacuation is triggered.
- Security: owner, idempotency, exact region revision, malformed native/relay input, stale confirmation, expired admission.
- Exact-head Windows and Linux native CTest + real host/relay E2E + independent SQLite custody and inventory audit.

**Gate close rule:** 4C.9A alone is not a completed transfer. Keep 4C.9 and 4C.8 native acceptance unverified until their exact-head build/test results pass.
