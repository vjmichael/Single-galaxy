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


## SHIP-STATUS-07 stealth, anonymous evidence and hailing continuation

The user clarified the gameplay rule: **no authenticated hail means no proven identity**. A player or NPC may refuse to hail to keep their identity hidden; an ECM-protected observer can still record the actual physical features of an otherwise unidentified hull. Higher-level security can investigate a *matching description* without inferring that all identical-looking ships are the same ship or that their owners committed an offense.

### Actual original ship capabilities

- **Stealth baffle**: original native `ConfigureCyberModule` 5/-5, installed in one of two finite support slots, consumes/returns an actually manufactured colony module. The observed radius of a baffled ship decreases from **5,000 mm to 1,200 mm** for ordinary sensors.
- **Powered survey scanner**: original fitted hardpoint and original effective-on module execution (including native Confusion or manual OFF) raises stealth acquisition to **3,000 mm**. No new scanner inventory or spoofed output.
- **Counter-ECM sensor array**: original `ConfigureCyberModule` 6/-6, also costs a fabricated support module, restores the **5,000 mm** stealth detection range and gives high-resolution anonymous detail. It does not automatically reveal an account or a pilot.
- Passive hull observations record repeatable physical description grades: optical hull/capacity and visible condition; stronger sensors see racks and fitted hardware. A manufacturing receipt or internally assigned entity ID is **not** a visual hull serial number.
- The same acquisition rule now runs in `ColonyDefense::observe_local_contacts`, in the player cyber target readout, in `ServerViewBridge`'s owner-scoped graphical visibility projection, and **again inside native `Action::HackConfuse` execution**. A guessed entity ID from a modified client must not bypass stealth.
- Both player and NPC ships share identical native equipment, physics, sensing and combat executors. An attacker can become detectable by approaching inside the reduced range or the defender using original powered high-resolution hardware. This milestone does not add permanent magical invisibility.

### Original graphical privacy

For an authenticated owner-scoped SGVIEW stream, a foreign ship below the original sensor acquisition threshold is absent, while a detected ship appears as an anonymous track (`owner_id=0`) with foreign private cargo suppressed. Original native `Simulation` still retains the correct owner and cargo for all legal and physical operations. Track changes produce the existing native Remove/Upsert delta behavior. The legacy unauthenticated test adapter (viewer owner 0) remains unchanged; this is a local single-owner graphical stream, **not a complete new multiplayer information-security transport**.

The `CYBER_SENSORS` committed native host telemetry drives actual fit/remove and sensor status controls. The `CYBER_DEFENSE` and `CYBER_INCOMING` frames expose source track ID **zero** when an active attacker's vessel is not acquired. The cockpit displays *unidentified ship/source*, rather than inventing an owner. Foreign NPC supervisor's planned cyber orders are not broadcast as generic player telemetry.

### Incident case anonymity, subsequent verified hail and original identity

The SHIP-STATUS-07 original SQLite `sg_ship_status07_incidents` table receives additive version-2 `observed_description`, `sensor_detail`, `record_version` fields. Legacy draft v1 cases keep their original digest and can be read by the migrated schema, but their older owner disclosures cannot be retroactively considered trustworthy observational evidence. New intake seals the original attacking hull ID for private native provenance, sets `attacker_owner=0` and exposes only the recorded physical description. The normal case `summaries()` output redacts the sealed hull identity as well.

A **later** native `TerritoryJournal` authenticated `NoticeAcknowledged` or `NoticeRefused` for that exact physical hull and jurisdiction, in the current original world generation and matched by a currently duty-eligible observer, can append a *separate* immutable `sg_ship_status07_hail_links` record. This attaches that controller's identity and the original hull to pre-existing cases **without editing or reinterpreting the original evidence**. It proves who authenticated and controlled the hull during the **later** hail, not who was piloting it at the earlier incident. If the hull changed hands, the human/NPC culpability still needs independent evidence. Hailing by visual lookalike alone cannot link cases.

Both casebook and hail-link tables use immutable append-only SQLite triggers, bounded records, chain checksums and original-world backup/restart. They are not digitally signed or protected from a malicious database administrator. Case and hail links never grant crime findings, force, arrest, seizure, or punishment.

### Security-tiered reactions

`CyberSecurityResponse.hpp` operates only on actual named NPC observations and casebook descriptions. Ordinary sensors can record a visual *possible match*. An effective original survey scanner allows *watch for hail*. A genuinely installed counter-ECM array can make a **nonviolent original physical Patrol rendezvous** eligible after an independently authorized host request. A match never proves the criminal's identity; no threat/hostility labels, automatic convictions, free NPC ships, arrests or weapon attacks are created.

### Regression and rollout status

- **Added native tests:** `SHIPSTATUS07_StealthSensorTests.cpp` (material accounting, weak vs strong/powered sensor range, native guarded guessed-ID hack denial, v103 canonical save/restart), `SHIPSTATUS07_ViewPrivacyTests.cpp` (owner-scoped SGVIEW invisible/visible deltas, genuine owner redaction and recovery), and expanded `SHIPSTATUS07_CyberIncidentJournalTests.cpp` (anonymous case, exact original receipt, later verified hail, mistaken identity controls, graduated patrol, immutable linked tables and SQLite copy).
- Existing `SHIPSTATUS04_NpcCyberTacticsTests.cpp` now expects anonymous original witnessed owner; expanded `SHIPSTATUS05_CyberRelayTests.py` has strict physical stealth fitting schemas; expanded `SHIPSTATUS05_CyberCockpitTests.js` tests unidentified event source and sensor controls.
- **Version repair:** `galaxy-kernel-v103` serializer, version reader and original support-rack legal hardware codes have been updated together; legacy v102 save structure remains readable.
- **Direct V8 execution:** actual branch cockpit parser + DOM wiring test suite ran with a Node/DOM compatibility shim and passed, including malformed-frame refusal, anonymous incoming effects, stale sensor telemetry and enabled/disabled physical fitting.
- **Windows/Linux C++ CTest and actual Chromium/installed-game verification:** NOT YET EXECUTED/PASSED; GitHub native runner jobs have remained queued. The PR must remain draft.
- **Not yet integrated:** the production authenticated native host does not automatically instantiate the territorial casebook or publish its redacted incident summaries, nor does it offer an authenticated territorial-hail response UI. Current real host has the fitting, sensing and anonymous combat telemetry wired; casebook/hail matching are genuine native domain APIs and tests, **not yet end-to-end interactive player case-management features**. They remain the next bridge work, not grounds to claim game UI acceptance.

Security limitations to address next: world-stream command-result metadata scoping and a complete authenticated player/NPC hail transport with post-hail report publication. Both should reuse original transport and avoid a second game writer or independent justice engine.
