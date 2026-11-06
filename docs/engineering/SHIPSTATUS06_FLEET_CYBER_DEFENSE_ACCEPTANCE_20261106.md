# SHIP-STATUS-06 — Physical NPC Fleet/Cyber Coordination and Defense Feedback

**Implementation status (2026-11-06):** coded on a stacked draft above SHIP-STATUS-05 PR #82. **Native Windows/Linux compilation, authenticated real-host smoke and installed graphical browser acceptance are NOT yet signed off.** Do not substitute a green source review, a queued GitHub job, or a contract test for real execution.

## Reused systems and responsibilities

- The ORIGINAL `NpcFlightOrder`, `NpcFlightPurpose::Patrol`, `PersistentWorld::queue_npc_ship_rendezvous`, native `process_npc_flight_navigation`, and `ActivityKind::NpcRendezvous` drive **actual physical approach**. No alternate combat flight physics or teleportation.
- The ORIGINAL `ColonyDefense::Contact`, named NPC crew check, `ColonyConflict` governance, `ColonyDispositionCoordinator::dispatch_witnessed_response`, `NpcResponseDispatch::dispatch_observed_patrol`, and immutable diplomatic-intent records decide *whether* predatory actors may make a hostile approach. A generic observation never grants an attack.
- The ORIGINAL `NpcCyberTactics::dispatch_governed_hack`, `PersistentWorld::submit_npc_cyber_attack`, and canonical `Action::HackConfuse` apply cyber effects only after a **new** observed contact, finite cooldown and real powered emitter. Native command execution preserves target range, target status, defenses, identity, material, and SQLite recovery.
- A patrol order **cannot** authorize lethal weapon fire, arrest, boarding, seizure, or crime adjudication. The present original kinetic combat only has training weapons versus practice drones. Coordinated live-fire NPC versus NPC combat still requires a separate physical weapon/authority milestone.

## Host supervision

`NpcCyberSupervisor::supervise` continues on committed 5-tick pulses with a two-operation ceiling and a 32-current-contact observation ceiling. A real observed target farther than 3,000 mm but within the original verified 5,000 mm watch range prompts an original `Patrol` rendezvous if the original strategy is `PrepareRaid`. After the physical flight completes and duty is available, the next eligible pulse may issue the original `HackConfuse`. Already-active original NPC flight reservations prohibit concurrent cyber work; the same named NPC is never silently duplicated.

Flight order IDs are deterministic in [2^39, 2^40), cyber command and evidence IDs remain in [2^49, 2^50). The original flight and command ledgers independently refuse duplicates. Sources and ship IDs are reconstructed from real world state. Crucially, because `PersistentWorld` swaps its internal `Simulation` on every commit, the supervisor now **snapshots entity IDs before iterating** and never holds entity-map iterators/pointers across durable writes. This also repairs a potential pre-existing invalidated-iterator hazard when multiple ships were present.

Host telemetry distinguishes `NPC_CYBER_APPROACH_QUEUED` from `NPC_CYBER_QUEUED`, and neither is represented as an applied attack.

## Player defense

The *existing* `viewer/interactive.html` now has accessible live fields:

- `CYBER_DEFENSE|ship|remaining_ticks|source_ship|exposed_module_count`: authoritative current-tick status projection from the **native active effects** only; zeros when no effect.
- `CYBER_INCOMING|victim_ship|attacker_ship|actual_command_id|actual_outcome`: emitted only after a committed NPC-attributed attack event targeting the actually piloted ship. `APPLIED`, `ATTACK_RESISTED`, or another native refusal are visibly distinct.
- Fitted firewall, effective hardpoint power, manual isolation and original requested settings are still read from `CYBER_STATUS` / `CYBER_MODULE`. Isolation is never mislabeled as Confusion. Target selections and fitted emitter power must belong to the **current** committed native tick before an attack button is enabled.

The UI never decides crime, hull damage, effect duration, successful attack, or weapon authority on its own. An HTTP 202 receipt is **queued**, not a successful action.

## Verification inventory

| Gate | Evidence required | Current status |
|---|---|---|
| Linux native compilation & tests | CMake native `galaxy_ship_status06_fleet_cyber_tests` and SHIP-STATUS-01…05, real host and PLAY-10 | Awaiting runner execution |
| Windows native compilation & tests | Same native targets with SQLite via vcpkg | Awaiting runner execution |
| Native patrol-to-cyber | `SHIPSTATUS06_FleetCyberTests.cpp`: physically manufactured emitter, original government, real watch, native patrol movement, rendezvous receipt, fresh attack, material conservation, canonical SQLite copy/restart | Committed, not yet run |
| Authenticated live game | `SHIPSTATUS06_LiveCyberHostTests.py`: host + relay + shipping bridge, owner session and foreign rejection, mine/refine/fabricate, fit/power/isolate, invalid target refusal, actual committed cyber telemetry and persistence | Committed, not yet run |
| Browser logic | `SHIPSTATUS05_CyberCockpitTests.js` expanded with defensive frames, real DOM controller and malformed telemetry tests | Committed, not yet run as Node suite |
| Direct V8 execution | Actual branch `viewer/cyber-controls.js`: initialization, actual powered-button dispatch, isolation, incoming effect message, stale current-tick target | **Passed direct V8 execution in development session** after fixing an initialization null defense dereference |
| Real Chromium browser (Linux CI) | `SHIPSTATUS06_InstalledBrowserTests.py` launches the original relay + native host + shipping bridge and actual headless Chromium, signs in, manufactures/fits an emitter, isolates/reconnects through real UI controls, verifies absent foreign target, JS console, logout and SQLite restart; workflow installs Playwright + Chromium | **Committed and queued**, not yet executed on runner |\n| Installed UI (graphical Windows) | Open the Windows installed game, login; inspect fitted gear, status, defense, foreign contact, and restart; inspect console/network and executable packaging | **Pending physical installed Windows acceptance**, not simulated by Linux Chromium |

## Follow-up priorities

1. Get real GitHub Actions Windows/Linux runner steps rather than all-queued/zero-step failures; repair any actual compiler, Python, Node or CTest diagnostics at the **exact new PR head**.
2. Run the authenticated integration test and then a full graphical installed-browser smoke on the same real native save (especially a physically existing opposing NPC ship); mark each distinct gate only on evidence.
3. Expand physical kinetic ship-versus-ship combat *separately* through manufacture and authority; avoid extending training drone damage into civilian ships without rules.
4. Add independent faction-law adjudication of witnessed cyber aggression. Existing territorial complaint, a predatory raid decision, and a successful hack must NOT imply a legal conviction.
