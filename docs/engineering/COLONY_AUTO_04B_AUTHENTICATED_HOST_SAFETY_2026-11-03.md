# COLONY-AUTO-04B — Native host enforcement and immutable life-safety admissions

**Date:** 2026-11-03. **Status:** Verified *host safety admission and telemetry*; not yet real production disabling, NPC evacuation, GRSM knowledge propagation or full cold/detailed behavioral scheduler.

## Added to the existing unified C++17/SQLite game kernel

- `PersistentWorld::record_colony_safety_preflight()` computes the v65 policy decision on the actual loaded `Simulation` and writes a durable decision with the SAME `Store::db_` SQLite connection, inside one `BEGIN IMMEDIATE` transaction. No second inventory authority or parallel writer is opened.
- Rejected occupied-site safety orders (seal exit, disable distress, atmosphere dumping, block emergency repair) record an explicit **attempted order** with actor/owner/site/tick, *not* an omniscient inference of motive. They never enter the simulation's ordinary command queue.
- A separate `sg_colony_safety_meta` count/tip anchor updates atomically alongside the evidence. On world reopen, `PersistentWorld` rejects missing or partial evidence tables and mismatched counts/tips. This is local tamper detection, **not** cryptographic protection against unrestricted database modification.
- Production-policy `PRODUCTION_PREFLIGHT` is read-only with respect to production and consumption. It logs the policy assessment; there is NO production-toggle implementation in this gate.
- Native commands `SAFETY|site|ASSESS`, `SAFETY|site|PRODUCTION_PREFLIGHT`, and corresponding *native-only* hazardous-order vocabulary resolve through the authenticated owner and real compartment topology. No UI control exists for harmful native commands; forged direct pipe commands remain refused and recorded.
- The existing Python colony relay admits only two safe operations (`colonySafetyAssess`, `colonySafetyPreflight`) under the v64 account/CSRF/origin checks, never client-provided ownership or direct SQL. A principal other than the owner is refused.
- The original colony UI displays live `SAFETY` telemetry derived from authoritative food/water/oxygen runway, and labels the shutdown assessment **preflight only**. Site ID is learned from native telemetry, not assumed to be entity 1 on restarted worlds.

## Test evidence

1. `galaxy_colony_auto04a_safety`: read-only stock assessment, existing immutable standalone evidence prototype, and real macro consumption.
2. `galaxy_colony_auto04b_host`: true `PersistentWorld` shared DB connection; six allowed/rejected orders; foreign player refusal; no change in real food/crew/ordinary operation count; injection of failing SQLite trigger rolls back evidence + count; matching evidence survives restart/world tick; malicious row deletion fails closed upon reopening.
3. `galaxy_colony_auto04b_authenticated_relay`: native authenticated pipe refuses player 8 and physically lethal occupied-site orders, while player 7's production preflight is logged. Restart retains evidence. Real provisioned account login to original Python relay refuses anonymous access, cross-player access, missing CSRF, cross-origin POST, boolean-as-site and spoofed `playerId`; genuine owner receives 202 and the native world commits exactly one new audit fact.
4. Existing colony, original shipping, regional handoff, save replay and game-host regression tests should be rerun after source publication; do **not** claim full acceptance without those results.

## Remaining implementation stages

- Real configurable service production policy in the canonical game state (no hidden consumption disable); physical dependency planning and intercolony supply agreements.
- Automatically protected evacuation/repair/work refusal using actual vehicles, berths, staffed equipment and survival priority, with rollback/replay and no free persons or materials.
- Witness-scoped social-memory dissemination and graduated grievance/voluntary migration distinct from immediate self-preservation.
- Machine-readable transport results (202 means queued, not committed), production Windows executable, graphical browser acceptance, and native concurrency/worker containment.

**Safety policy:** preserve non-graphic dependent presentation, exact guardian custody and the existing fatal-dependent refusal until validated separate migration gates exist. No player-facing lethal hazard action is added.
