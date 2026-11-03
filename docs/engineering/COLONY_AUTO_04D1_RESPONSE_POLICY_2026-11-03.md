# COLONY-AUTO-04D.1 — Deterministic survival response policy

Date: 2026-11-03. Branch: `integration/colony-auto04d-response-policy-v68-20261103`.
Base: AUTO-04C/v67; implementation commit series continues the native v66 authority.

## Delivered

The `ColonySafety` module now computes a deterministic **read-only** response plan from the existing authoritative `Assessment`, and a bounded per-site risk transition. The plan depends on the actual occupied compartment, crew, physical food/water/oxygen endurance, and current committed simulation tick; it never depends on morale, player permission to survive, a promised shipment, or a fabricated transport.

| Physical risk | Response priority | Requested actions (not executed) |
| --- | --- | --- |
| Safe | 0 | None |
| ForecastShortage | 1 | Monitor and request physical supply |
| Critical | 2 | Request supply, find real repairs and possible transport, bypass production preference |
| Emergency | 3 | Same as critical plus independent distress signaling |

The response includes `rescue_completed=false` **always**. It must never be interpreted as a real rescue, real repair, spawned cargo, or actual production mutation. The native colony host publishes `SAFETY_RESPONSE|site|level|request_supply|seek_repairs|search_departure|distress|rescue_completed` separately from the authoritative `SAFETY` stock telemetry.

`response_transition` requires identical physical compartment identity and a non-rewinding tick, and marks one notice per changed risk threshold. The caller must persist/dedupe such notices before enabling player/NPC-facing notifications across restarts. It is currently a stateless comparison, **not yet a durable event scheduler**.

Existing tests were expanded to check the successive forecast/critical/emergency transitions against physically consumed stock (100→16→4→0 kilograms), live owner-scoped refusal behavior, stale/cross-site transition rejection, preservation across canonical world snapshot restoration, and absence of phantom crew/rescue. The existing live-host test now expects the initial safe response telemetry.

## Non-goals / outstanding

- The current policy generates *intent*, not executed stop/resume replenishment, physical production dispatch, or repairs.
- No service has yet journaled an autonomous alert; repeated host restarts must **not** claim exactly-once notifications until a durable anchor exists.
- Mutual aid and evacuation must be validated against committed donor inventory, route, seats, ship condition, ferry crew, guardian/dependent roster and recipient custody.
- Player attempts are source facts, not inferred motive. GRSM memory needs individually witnessed source evidence and contact provenance.
- This branch has not passed full CTest or Windows host acceptance until a GitHub Actions job or independent native compilation reports success.

## Next gated work: AUTO-04D.2

Introduce durable, generation-scoped per-site response transition state in the canonical SQLite writer and perform one transactional scan on **each committed tick**. Persist monotonic event identity, site, before/after risk, world generation, assessment tick, inventory witness, and single-writer producer id. Audit restart/replay and cold/warm handoff. Only then dispatch real repair/supply/evacuation actions through existing authorized physical-world commands; do not offer a phantom single-click 'save colony' operation.
