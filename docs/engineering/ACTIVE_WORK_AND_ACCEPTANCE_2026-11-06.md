# Single-Galaxy — Current engineering work ledger and acceptance gates

**Recorded 2026-11-06. Project:** the **original Single-Galaxy game**, not the separate EVEmu audit. **Source of truth:** the real repository branches, native C++/SQLite authoritative world, authenticated host, executable tests and exact commit-specific results. This ledger tracks the active *SHIP-STATUS / cyber / hailing / security* stack and its direct dependencies. It is not a claim that the full one-galaxy game is finished.

## Where development currently stands

The game has a historical Linux proto­type and extensive older design/engineering reports. **Do not mistake those historical pass counts for current-stack acceptance.** The newest work is an **unmerged, sequential stack of nine OPEN DRAFT PRs**:

| PR | Implementation checkpoint | Status |
|---|---|---|
| [#78](https://github.com/vjmichael/Single-galaxy/pull/78) — SHIP-STATUS-01 | Temporary physical-module Confusion effect | Draft; native current-stack acceptance pending |
| [#79](https://github.com/vjmichael/Single-galaxy/pull/79) — 02 | Original authoritative module execution, v100 persistence | Draft; native acceptance pending |
| [#80](https://github.com/vjmichael/Single-galaxy/pull/80) — 03 | Manufactured intrusion emitter, firewall, v101 | Draft; native acceptance pending |
| [#81](https://github.com/vjmichael/Single-galaxy/pull/81) — 04 | Real NPC-operator cyber decisions, territorial evidence, v102 | Draft; native acceptance pending |
| [#82](https://github.com/vjmichael/Single-galaxy/pull/82) — 05 | Player cyber cockpit and recurring supervised NPC cyber executor | Draft; installed/native acceptance pending |
| [#83](https://github.com/vjmichael/Single-galaxy/pull/83) — 06 | Real NPC approach/patrol → cyber action; native defender status | Draft; native/installed acceptance pending |
| [#84](https://github.com/vjmichael/Single-galaxy/pull/84) — 07 | Anonymous physical ship description, case intake, v103 stealth and sensor acquisition | Draft; native/installed acceptance pending |
| [#85](https://github.com/vjmichael/Single-galaxy/pull/85) — 07D | Authenticated territorial and direct-security desk bridge; voluntary hailing, warning/reply, case presentation | Draft; native/installed acceptance pending |
| [#86](https://github.com/vjmichael/Single-galaxy/pull/86) — 07E | Direct ship hails, Identify/Decline, durable anti-harassment penalties, owner-scoped complaints | Draft; native/installed acceptance pending |

Each PR depends on the preceding one. **Code committed ≠ accepted feature ≠ merged mainline.** Do not mark a subordinate PR green because a later branch contains its source; require actual acceptance for the integrated chain. The active branch currently audited is `feature/ship-status07e-hail-abuse-escalation-20261106`, built on PR #85 and ultimately #78.

## Binding rules — ship identity, evidence, stealth and abusive hailing

1. Players and NPCs can choose **not to hail**. A silhouette, scanner contact, internal world owner ID, criminal-looking ship or valid ECM contact is not an authenticated personal identity. Sensors may see a descriptive ship track without an owner, and equipped stealth reduces actual native detection. Powerful physical scanning may recover the vessel **without revealing a pilot identity**.
2. An authenticated **voluntary hail** reveals the **present** owner/controller of the real hull to the relevant recipient. It can associate prior anonymous incidents with that hull, **but does not prove the same controller committed an earlier offense** or automatically establish legal guilt. Responding to a territorial warning and refusing a direct hail are different events.
3. A first successful **direct ship-to-ship hail** goes to a real sensed recipient. That recipient independently chooses **Identify**, **Decline**, or silence. The caller's authenticated account is known to the recipient; the recipient is not forced to identify. Unanswered calls are suppressed while pending; a Decline keeps identity undisclosed and prevents repeat contact for a limited period **without generating a crime score**.
4. **After the recipient has actually identified itself to this caller**, later unsolicited hails are **blocked before delivery** and scored, at most one increment per offender/recipient per committed world tick. A previously verified territorial identity receipt to the same calling colony may provide independent prior proof. No client-supplied player ID, evidence or warrant is trusted.
5. **The detailed abuse ledger stops at ten per offender–recipient pair, not the score.** The first **10** strikes retain separate immutable SQLite evidence receipts. From **strike 11 onward**, one compact per-pair row increments atomically for each new offending tick; additional individual detail rows are **not** manufactured. The gameplay score continues upward and sums **across recipients**, within the representable signed-63-bit storage bound; it is **not capped at ten**. The latest compact total survives SQLite copy/restart, with a monotonic SQL update guard. Compact totals do **not** constitute a separately immutable record of every later strike. No malicious-DB-administrator resistance is claimed.
6. Outbound direct-hail mute duration is based on the strike against that target: **200 / 800 / 2,400 / 7,200 ticks** for strikes 1 / 2 / 3 / 4+, approximately **5s / 20s / 60s / 180s** at 25 ms per game tick. An unanswered initial request can remain pending 2,400 ticks, a declined request inhibits further calls for 1,200 ticks. Tick-rate changes change real elapsed time. Existing incoming replies, distressed/rescue communications and lawful warnings are not unsolicited outbound hails.
7. At **3+** proven nuisance points, a case can become **eligible for separate security/law-enforcement review**. The review system must check actual jurisdiction, original evidence, duly authorized NPCs/institutions and permitted remedies. **No matching silhouette, complaint, refusal or score automatically grants criminal guilt, force, arrest, confiscation or a police fleet.** Law enforcement that does not physically exist cannot respond.

**Code/acceptance distinction:** the above is the binding design/implemented draft behavior. Native C++ behavior and save/restart must still be proven under actual runnable builds; test source and JavaScript shims alone are not acceptance.

## Exact verification evidence at this checkpoint

- **Native CI:** the latest inspected original cyber-status workflow run for PR #86 at code head `57a26b24923cc6a9049441d2b99a007ec94c0ac6` was [run #42304676046](https://github.com/vjmichael/Single-galaxy/actions/runs/42304676046). Both `native (windows-latest)` and `native (ubuntu-latest)` were **queued, without executed build/test steps or conclusions**. This is **not** a compiler failure, nor a pass. A later documentation-only branch commit does not retroactively change this evidence; refresh the exact SHA and runner steps before claiming new status.
- **Executed client validation:** both the actual branch `tests/SHIPSTATUS07D_SecurityDeskTests.js` and `tests/SHIPSTATUS07E_HailAbuseCockpitTests.js` passed direct V8 execution with Node/DOM shims, including exact signed-63-bit decimal IDs, score displays over ten, permissions and signout reset. This does **not** substitute for installed Chromium or Windows acceptance.
- **Test sources committed but unverified natively:** C++/SQLite cases `galaxy_ship_status07e_hail_abuse`, `galaxy_ship_status07d_security_desk`, `galaxy_ship_status07_incident`, `galaxy_ship_status07_sensors`, `galaxy_ship_status07_view_privacy`, plus preceding SHIP-STATUS native suites; authenticated relay + host Python smoke; Linux Playwright browser test.
- **Actual GitHub workflow scope:** builds C++ original game host and adapter on Windows/Linux, executes native CTest and original authenticated relay smoke, and runs headless Chromium on Linux against a **source-built game process**. It currently has **no packaged Windows installer build, install/launch/uninstall or independent installed graphical Windows test.** Renamed the browser CI step to avoid mislabeling it as installed-app acceptance.
- Original game opening remains a single colony ship, finite crew, **no pre-seeded criminals or police**. Native two-owner fixtures are only fixtures; the current desktop local-account relay supports a single owner, not a complete multiplayer host.

## Outstanding work — prioritized and independently checkable

| Priority | Task ID | Next concrete work and required evidence | State |
|---|---|---|---|
| P0 | **STATUS-VERIFY-01** | Obtain actual **exact-head native Windows/Linux CMake compile + full relevant CTest**, including 01–07E. Inspect real job steps/logs when runners execute; repair actual compile/runtime failures. Do not infer code success from queued jobs. Can use independent native runners without waiting on GitHub if available. | **Open — verification** |
| P0 | **STATUS-VERIFY-02** | Run physical two-owner hailing cases on actual original C++/SQLite: Identify/Decline, repeated same-tick suppression, 10 original detail rows, strikes 11/12 and 3+ review, aggregation across ships, monotonic counter triggers, copy/restart, host nonce, replay/denied principal, stealth detection and casebook integrity. Verify tests truly assert the intended behavior, not just fixtures. | **Open — verification/repair** |
| P0 | **STATUS-VERIFY-03** | Run authenticated native relay and real Chromium end-to-end after compile: login → fit modules → scan/detect → direct hail → answer → redundant blocked call → penalty → case/readout → logout → restart, preserving real per-owner scoping and no fabricated NPC/police. Where current single-owner relay cannot provide the second end user, add trusted multi-account acceptance fixture or state the limitation. | **Open — integration acceptance** |
| P0 | **STATUS-INSTALL-01** | Create or reuse a **real Windows game package/installer** (with the intended game launcher, host, relay, viewer, dependencies/assets), then execute install → launch/play → save/restart → uninstall on a Windows runner/VM; collect evidence. Existing CI **does not perform this**. Packaging choice and license/dependency audit remain required. | **Open — packaging + installed-game acceptance** |
| P1 | **STATUS-COMMS-01** | Complete authenticated *two-sided* player-to-player and player-to-NPC/ NPC-to-player hailing through one real host/relay, including recipient inbox, opt-in identification, unanswered/declined contact, trust boundaries and disconnected/reconnected recipients. Current native C++ fixtures ≠ multiplayer or autonomous NPC reply behavior. | **Open — implementation** |
| P1 | **STATUS-LAW-01** | Connect review-eligible nuisance cases to **existing real colony security institutions / lawful NPC response**, only when the jurisdiction actually has adequate enforcement resources. Add independent disposition/review decisions, notification and appeal/correction; prohibit automatic convictions, force, arbitrary police spawns or retroactive pilot blame. | **Open — implementation** |
| P1 | **STATUS-SEC-01** | Audit end-to-end privacy: owner-scoped SGVIEW, observer evidence detail, stale contact/replay/cross-account leakage, full 63-bit wire IDs, server-side actual targeting/stealth, case-report visibility, rate and storage exhaustion under hostile hailing. Run adversarial native/installed integration tests. | **Open — hardening/verification** |
| P1 | **STATUS-STACK-01** | Reconcile original territorial DISPOSITION/NPC-response dependencies and the entire stacked #78–#86 chain. Once native/installed gates pass, merge in dependency order, rerun on merged exact heads, update current source and cut a reproducible release candidate. Do **not** merge on JS-only checks. | **Open — integration/merge** |
| P2 | **STATUS-NPC-01** | Extend NPC maritime/cyber tactics to observe nuisance communications and respond proportionally through the existing named-NPC work/flight/territory system, without new instant ships or extra authority. Real NPC construction, training and lawfulness stay required. | **Open — gameplay expansion** |
| Separate | **COMBAT-PHYSICAL-01** | **Lethal physical ship-to-ship weapon combat** (actual damage and ships fighting), including criminal authorization, salvage/custody and recovery. Existing training combat and temporary cyber status effects do not fulfill this. **Not a prerequisite** for closing hailing/abuse verification. | **Deferred separate milestone** |

This table is scoped to the **active cyber/security/hailing stack**. The much larger planetary, settlement, industry, migration, social-memory and continuous-galaxy roadmaps remain separate workstreams; do not mark those complete or blocked solely because this stack is awaiting verification.

## Practical next order

**First** complete P0 verification and native repairs (STATUS-VERIFY-01/02). **Second** execute authenticated real UI end-to-end (STATUS-VERIFY-03). **Third** produce installed Windows evidence (STATUS-INSTALL-01), while independently implementing legitimate cross-owner/NPC comms (STATUS-COMMS-01) without treating queued CI as a coding blocker. **Then** harden owner/privacy and connect legitimate enforcement; finally merge accepted stack. Maintain this ledger after each verified head and report the actual acceptance evidence.

### Related primary records

- [SHIP-STATUS-07E – hail abuse rule and limits](SHIPSTATUS07E_HAIL_ABUSE_ESCALATION_20261106.md)
- [SHIP-STATUS-07D – authenticated security desk](SHIPSTATUS07D_AUTHENTICATED_HAIL_SECURITY_DESK_20261106.md)
- [SHIP-STATUS-07 – anonymous cyber cases and sensor privacy](SHIPSTATUS07_CYBER_INCIDENT_INTAKE_20261106.md)
- [SHIP-STATUS-06 – real NPC physical cyber patrol verification](SHIPSTATUS06_FLEET_CYBER_DEFENSE_ACCEPTANCE_20261106.md)
- [Design decisions register](../design/Core_Decisions_2026-10-31.md)
