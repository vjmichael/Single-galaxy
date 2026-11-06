# SHIP-STATUS-05 — Cockpit Controls and Recurring NPC Cyber Supervisor

**Source state:** committed to stacked draft on 2026-11-06. Native Windows/Linux and installed-browser acceptance **remain pending**. This document is a traceable implementation ledger, not a claim of a passing native release.

## Integration map

| Layer | Original authority | Work committed |
|---|---|---|
| Physical ship inventory | `Entity::fitting` and manufactured `ColonyShip.modules` | Existing SHIP-STATUS-03 intrusion emitter and firewall unchanged |
| Native commands | `Simulation::submit/step`, `PersistentWorld::submit` | Existing `ConfigureCyberModule`, `SetHardpointPower`, `SetCyberIsolation`, `HackConfuse`, guarded by original owner and cargo/crew rules |
| Player authentication | Local `interactive_relay.py` and native private-stdin host | Four strictly validated actions, no client issuer/actor/command ID/tick; original player 7 authority bound by relay and independently by native host |
| Native cockpit read model | `galaxy_colony_live_host` committed world | `CYBER_STATUS`, `CYBER_MODULE`, `CYBER_TARGETS_RESET`, `CYBER_TARGET`, `CYBER_RESULT` on original `COLONY|1|tick` channel |
| Player UI | Existing `viewer/interactive.html` and `interactive.js` | Cyber status panel, fit/remove, power and isolation controls, legitimate nearby targets, attack request and actual committed outcome |
| NPC strategy | Original `ColonyConflict` and `NpcCyberTactics` | `NpcCyberSupervisor::supervise` reads committed predatory leadership, physically installed emitter, real named crew, fresh current sighting |
| NPC effect execution | Original `Action::HackConfuse` and SHIP-STATUS-01/02 | One identical real command/effect pipeline, cooldown, resistance, original SQLite world snapshot v102 and immutable operator attribution |

## Player-facing behavior

`viewer/cyber-controls.js` is a standalone strict telemetry reader; it never computes an authoritative effect or creates inventory. It allows the real original player to request fitting/unfitting an emitter or firewall, switch fitted hardpoint power ON/OFF, isolate/reconnect an installed hardpoint, and attempt Confusion on a genuinely visible foreign physical ship within the native emitter range. Controls are disabled without a committed fitted emitter or while a cooldown is reported. The host validates all state independently.

The ordinary live relay validates exact JSON fields and types, enforces same-origin session/CSRF, maps to bounded private pipe `CYBER_FIT`, `CYBER_POWER`, `CYBER_ISOLATE`, `CYBER_ATTACK`, and never permits browser-selected owner, source hull, authority or command ID. The original native host rebinds the active piloted hull, posts canonical native commands, and emits `CYBER_RESULT` only after a real `world.step()` receipt. HTTP 202 means **queued**, not successful hacking.

The original new-colony genesis has no free opponent faction or cyber weapon. The target selector legitimately stays empty until an actually present qualifying foreign ship exists. Training drones are excluded from hacking targets; existing training weapon UI is separate. Existing physical manual flight remains available during Confusion.

## Bounded recurring NPC supervision

`NpcCyberSupervisor.hpp` uses a **world-tick-driven** 5-tick interval, two authorized NPC attacks maximum per interval, deterministic original ship/NPC ID iteration, and current genuine physical contact. No background thread or alternate AI flight controller. It skips the authenticated player-owned ship, unfitted/unpowered modules, uncrewed or non-predatory colonies, absent governance/leader, ships already in cooldown, and same-tick previously admitted NPC attack orders. It records a real original contact and strategic PrepareRaid decision, then dispatches through SHIP-STATUS-04's original named-NPC `submit_npc_cyber_attack`. A refused or out-of-range command remains refused. Full native status v102 retains immutable operator/observer IDs and effects. A predatory colony may initiate an attack even without criminal provocation, but attacking carries **no presumed legal justification or victim guilt**.

The host executes this once after each committed `world.step` (not while SQLite is uncommitted). A queued NPC cyber attack is announced as `NPC_CYBER_QUEUED`, not as a completed hit. The original next tick still decides module activation, actual target range, cooldown, resistance and real outcome.

## Acceptance matrix

**Native:** `tests/SHIPSTATUS05_SupervisorTests.cpp` constructs three real colonies, finite real resources and NPCs, original predatory leadership, actual material fabrication and emitter installation. It verifies no early off-cadence attacks, exactly one first attack, duplicate suppression, native restart at pending command, true Confusion effect and no hull/material changes, cooldown refusal on tick 10, second authorized tick-15 attack, native v102 replay, and independent SQLite `recover_copy`.

**Browser:** `tests/SHIPSTATUS05_CyberCockpitTests.js` checks strict numeric/state/outcome decoding and disabled-attack criteria. `tests/SHIPSTATUS05_CyberRelayTests.py` invokes the actual relay `Handler.do_POST` with a private local runtime stub; verifies exact emitted native pipe commands for valid actions and rejection of additional owner/command fields, invalid types, session failures and cross-origin calls. Those contract tests do not pretend the full installed app rendered.

**CI:** Expanded original `ship-status02-authoritative.yml` Windows/Linux matrix to build `galaxy_colony_live_host`, SHIP-STATUS-01/02/03/04/05 and the original PLAY-10 regression. Runs CTest native tests, Python 3.11 relay checks, Node 22 cockpit checks, source syntax checks. Prior native CI runs ended failed with zero reported job steps, and GitHub returned 404 BlobNotFound when retrieving their logs. This does **not** establish that a compiler actually ran. No green acceptance is claimed until exact-head runs provide build/test receipts.

## Still outstanding

1. Actual Windows and Linux native compilation/test results and repair of whatever errors real compiler logs show.
2. Installed locally authenticated browser smoke test with real player cyber fitting/ON/OFF/isolation and a genuinely spawned/constructed foreign target; no owner spoofing.
3. Robust long-lived NPC tactical dispatch policy balancing patrol versus combat and physical fleet coordination, beyond the finite colony-raid supervisor.
4. Actual legal finding/adjudication source to bind same-tick witnessed cyber incidents and optionally apply the separate hail-acknowledgment 1.5× sanction recommendation. No accidental conviction.
5. More complete cockpit visual effects and defensive feedback, including accessible displays when Confusion is active.
