# DISPOSITION-01 — Unified Colony Disposition to Physical Response

**Engineering status:** source, native regression and Windows/Linux CI committed. Native acceptance pending; this is a draft, not a finished combat or detention gate.

## Reuse rather than rewrite

No fifth disposition ledger, duplicated AI runtime or replacement NPC autopilot was introduced. `ColonyDispositionCoordinator.hpp` is a thin adapter over the original durable systems:

1. **CONFLICT-01A:** founding cooperative/isolationist/expansionist/predatory agenda and real observed colony contact.
2. **CONFLICT-01B1/B2:** immutable GRSM witness/knowledge interpretations, dated previous-generation reputation and effective nonviolent diplomatic intent.
3. **CONFLICT-01B3:** authoritative current leader and governance policy, with later appointments/policy changes taking effect only according to the original generation fence.
4. **COLONY-SAFETY:** actual live life-support shortage and physically present crew, not a synthetic desperation score.
5. **NPC-RESP-03A:** transparent descriptive explanation of likely colony behavior, including predatory preparation versus lawful authority.
6. **NPC-RESP-01 and FLIGHT-03:** an independently rechecked named-NPC local watch and an original physical `NpcFlightPurpose::Patrol` command, qualified pilot, ship thrust and SQLite journal.

### Evaluation

`assess(PersistentWorld, colony, original_contact_event)` calls the **existing** `assess_colony_diplomatic_intent` and `colony_governance`. It verifies that the strategic decision refers to the same effective leader, published policy, agenda and governance epoch. For display, it maps the original `Posture` into a nonbinding `ThreatIntent`:

| Original evaluated posture | Display / potential response |
| --- | --- |
| Observe | Ignore, no patrol dispatch |
| Warn | Warn visitor; an actual patroller may approach to hail |
| RequestAid | Hail and seek assistance from the observed visitor |
| DemandSupplies | Threaten/request tribute as an intent, not automatic inventory seizure |
| PrepareRaid | Ambush preparation as intent, not an invented fleet or weapons |

The map to criminal, territorial, militant, cooperative descriptions comes from **the original agenda** and is not a fixed moral identity or a separate AI personality. The current leader and historical relation can change independently, preserving the distinction between a predatory founder, a reconciliatory successor, personal NPC opinions, and neighbors who still remember prior attacks.

No visitor invitation, territorial boundary, verified attack, guilt, warrants, weapon authority or right to seize goods can be inferred from merely seeing a foreign ship. Future durable access/territorial and law journals must supply those facts.

### Dispatch

`dispatch_witnessed_response(world, colony, contact_id, decision_event_id, flight_id, observer_npc, pilot_npc)` requires a non-observing original CONFLICT-01 strategy. It recalculates live local contact from the actual on-duty watch NPC, refuses departed/unseen contacts, records the strategy through the original immutable `record_colony_diplomatic_intent`, and delegates only a `Patrol` **physical rendezvous** to the original `dispatch_observed_patrol` function.

The diplomatic-intent journal and physical NPC-flight admission are deliberately independent transactions. Once a strategy has been recorded, a subsequently refused flight does not erase the witnessed intent. Neither creates combat damage, an arrest warrant, a passenger voyage, cargo movement or NPC records. `PrepareRaid` remains an intention until the original finite-fleet and weapon authority exists.

### Native regression

`galaxy_disposition01_unified_response` creates actual resource-backed cooperative, isolationist and predatory colony ships and one real foreign vessel. Original named leaders/guards, original agendas and original durable watch-contact events are used. It checks:
- isolationist warning, cooperative inaction, and predatory intent bounded by missing manufactured modules;
- zero fake guilt, firearm, arrest, seizure or material transfer;
- cooperative never creates a flight order;
- actual isolationist warning records the original strategic decision and posts an original qualified NPC `LocalRendezvous` patrol;
- physical flight arrives with applied original native control, foreign vessel remains undamaged, and original NPC crew/material remains conserved;
- snapshot roundtrip and independent SQLite close/reopen restore the same flight and world.

Exact-head Windows/Linux CI builds this fixture alongside the existing governance, diplomacy, and dispatch native acceptance targets.

### Next verified dependencies
- Live territory/visiting permission: source-of-truth region claims, invitations, warnings, grace period and re-entry incidents.
- Stable autonomy: a bounded host trigger that scans newly witnessed contacts with idempotent event IDs; no omniscient threat dispatch.
- Independent physical attack and disabling with munition, hull and real evidence rules.
- Distinguish lawful prisoner custody from unlawful pirate captivity, both using original independent player-person physical identity and durable transport rules.
