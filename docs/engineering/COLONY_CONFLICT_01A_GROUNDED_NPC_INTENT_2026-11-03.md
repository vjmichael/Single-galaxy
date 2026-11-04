# CONFLICT-01A — Witnessed NPC aggressive intent (v82)

**Source branch:** `integration/colony-conflict01a-aggressive-intent-v82-20261103`. **Parent:** v81 local watch.

## Invariants

An observed ship is NOT automatically hostile; the `ColonyDefense` crew watch is the authoritative position/duty bounded source. Every explicit political posture is tied to one named living colony NPC leader with a physical settlement. Colony agendas are host-approved founding dispositions (Cooperative, Isolationist, Expansionist, Predatory), **not** a historical verdict about another player. Unprovoked expansion or predation may occur because the colony chooses those goals. A scarcity-driven demand reflects real measured remaining food/water/oxygen, but does not claim the visitor actually carries any material.

`colony_conflict::evaluate` revalidates the actual current foreign contact, position, system, observer duty status and leader eligibility, then deterministically chooses one motive and posture. An adequately crewed and materially established Predatory colony can produce `PrepareRaid`; an Expansionist colony `Warn`; a close observed stranger provokes an Isolationist `Warn`; a Cooperative colony `Observe` unless genuinely short of life support, in which case `RequestAid`; a starving Predatory colony can `DemandSupplies`. Every case remains **non-executing intent** until physical weapons, combat crews and transport are implemented.

## Durable proof chain

The host explicitly calls `establish_colony_conflict_agenda` once for a colony, recording the real named leader, generation, tick, world hash, disposition and site in immutable SQLite. The leader must actually be physically present and duty eligible. Changing political policy later will need a new authenticated append-only governance transition; this first gate deliberately does not rewrite its founding declaration.

`record_colony_watch_contact` is required independently and provides the exact initial physical observation. `assess_colony_conflict_intent` is read-only; it requires a same-world-generation, same-tick, same-source contact and an existing authentic agenda. `record_colony_conflict_intent` writes a unique immutable decision with agenda-event and contact-event provenance, target owner/ship, motive, posture, source life-support risk, tick and physical world hash. It does not issue a weapon, move a unit, mark the player guilty, mutate reputation or spend resources.

`Store::verify_colony_conflicts` runs at ordinary startup and independent `recover_copy`, checks exact original founding snapshot + leader physical eligibility, recomputes each later decision using its pinned real observation/snapshot and rejects missing, reassigned or forged motives. Original agenda, observation and decision checkpoint generations are pinned in the ordinary world snapshot pruning SQL. Privileged offline tampering with an immutable intent field must fail even if the writer reinstates the trigger.

## Acceptance

`tests/COLONY_CONFLICT01A_AggressiveIntentTests.cpp` includes five real differently owned colonies in one system and an actual nearby player vessel. Predatory colony must first physically mine/refine/fabricate its modules before recording unprovoked `PrepareRaid`; Expansionist produces territorial warning, desperate Cooperative requests help using actual failing food, Isolationist warns about a nearby vessel, and safe Cooperative remains observant. Cross-colony false attribution and duplicate intents fail. No NPC or player ship is damaged by strategic decisions. Persistence across 120 ticks, a real independent copy and deliberate corrupted posture with restored trigger is verified. v81 crew watch test runs as regression.

**CI:** see dedicated `.github/workflows/colony-conflict01a.yml`. Verification must pass before calling this initial conflict gate complete; actual offenses, raids, warships and diplomacy revision are future gates.
