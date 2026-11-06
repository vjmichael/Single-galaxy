# SHIP-STATUS-07 — Independent Cyber Incident Intake

**2026-11-06 development checkpoint:** implementation and native regression source are committed in the stacked SHIP-STATUS-07 branch above SHIP-STATUS-06 PR #83. **Windows/Linux native execution and installed-game acceptance are pending.** The preceding PR #83 acceptance is also pending queued GitHub Actions runners.

## Scope

This wave establishes a durable security case *intake*, not a legal verdict, punishment, arrest, damage, or loss-of-freedom mechanism. A predatory agenda, hostile reputation, territorial warning, uninvited status, patrol, or submitted attack by itself is not legal proof. Both resisted and applied hacking attempts may be filed as witnessed incidents, with **different observed outcomes**.

The security case can be recorded only through the trusted native host:
1. `TerritoryJournal` has a real prior claim covering the site/attacker/target, and the original colony is independently owned; a naked player-supplied coordinates/claim is not accepted.
2. The current `PersistentWorld::recent_history` contains the **same-tick original `HackConfuse`** command ID, actor, target and immutable `Applied` or `AttackResisted` result.
3. An actual named, physically present, duty-eligible NPC assigned to the jurisdiction colony sees the attacker through `ColonyDefense::observe_local_contacts` on the same original tick.
4. `contemporaneous_cyber_evidence` from SHIP-STATUS-04 reconstructs these original facts. Intake accepts no free-form actor, target, owner, court verdict, sentence, or damage field.

`CyberIncidentJournal::intake` adds an immutable case receipt to `sg_ship_status07_incidents` in the **same original SQLite file**, under a separate transaction from the original world effect. No second Simulation or combat writer exists. If the intake transaction fails, original combat history survives unchanged and case insertion is rolled back; the case can be retried **while the original same-tick witness is still valid**. It is not safe to infer a case from a later rumor or stale event. Original-world backup copies the table with SQLite's `sqlite3_backup` mechanism.

Each case stores:
- unique case ID and original native attack command ID
- original world tick and generation
- jurisdiction colony and named eyewitness NPC
- attacker and victim owner IDs and physical ship IDs
- factual effect classification: attempted but resisted, or applied Confusion
- linked append-only sequence and integrity checksum

The checksum follows the repository's existing append-only evidence pattern; it is **not a digital signature or proof against a malicious database administrator**. SQLite triggers reject ordinary UPDATE/DELETE of committed case rows, and open/read validation checks sequence, prior checksums, identity uniqueness, and chronology. Unique database keys disallow duplicate cases and duplicate `(colony,command_id)` evidence.

`for_colony_owner(world, authenticated_owner, colony)` refuses foreign/invalid owners and only returns cases for an original existing colony ship whose actual owner matches the trusted account. The caller **must** bind `authenticated_owner` to its verified session, never to a client JSON field.

## Deliberate non-authorities

All `CyberIncidentSummary` flags `legal_crime_finding`, `arrest_authorized`, `damage_authorized`, and `seizure_authorized` remain false. `independent_law_review_required` is informational, not a permission to act. A future faction-law process must independently define jurisdiction, legal policy, evidentiary standard, adjudicator identity, due process, and appeal, and use original NPC response executors rather than adding force inside this casebook. No automatic security pursuit or retaliation is triggered by case intake.

## Native regression and acceptance

`tests/SHIPSTATUS07_CyberIncidentJournalTests.cpp` creates original physical colony ships, finite material and NPCs, builds an emitter through real mine→refine→fabricate→fit commands, claims territory through the existing `TerritoryJournal`, produces an original applied `HackConfuse`, and checks:

- Intake denial before attack, from wrong jurisdiction, forged witness, after the witness tick, and on duplicate case/command IDs.
- Injected crash after SQLite INSERT but before COMMIT: zero cases persist, original `Simulation::canonical_state` unchanged, independent read sees no partial record.
- Genuine original native evidence yields one immutable committed case with accurate owners, ships, named eyewitness and original command/result, but no criminal finding or arrest/damage/seizure authority.
- Owner-scoped reads deny an unrelated colony owner.
- Direct SQL UPDATE/DELETE are rejected by immutable triggers.
- `PersistentWorld::recover_copy` preserves both original canonical world and the casebook table; reopening and a later world tick do not create a duplicate.

CMake target: `galaxy_ship_status07_incident_tests`.
CTest name: `galaxy_ship_status07_incident`.
CI: existing `.github/workflows/ship-status02-authoritative.yml` Windows/Linux matrix adds this native test and retains SHIP-STATUS-01…06, PLAY-10, authenticated host and Chromium acceptance.

### Verification state

| Gate | State |
|---|---|
| SHIP-STATUS-07 code and native regression registered | Implemented / committed |
| Exact-head Windows compilation and native CTest | **Not verified** |
| Exact-head Linux compilation and native CTest | **Not verified** |
| Original authenticated game session and separate owner-facing security UI | **Not integrated by this wave** |
| Faction court, warrants, lawful arrest or force | **Intentionally deferred** |
| SHIP-STATUS-06 underlying flight/cyber native and browser acceptance | **Still pending** |

Follow-on work: a supervised security case desk integrated into the authenticated colony host (without a parallel world writer), plus future law/appeal authority. Keep this branch draft until real native test evidence is available.
