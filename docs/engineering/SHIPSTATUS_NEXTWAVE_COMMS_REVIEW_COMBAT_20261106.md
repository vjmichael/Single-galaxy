# Single-Galaxy — Next-wave comms, security review and physical ship combat

**Date:** 2026-11-06. **Code location:** consolidated draft PR #86, based on the original DISPOSITION-03B/C branch. **Not merged into main.** These are development source changes plus executable regression **definitions**, not completed native Windows/Linux acceptance.

## User-selected execution order

1. **Task 8 completed (review management).** SHIP-STATUS PRs #78–#85 are **closed as superseded** and their ancestry/code remains in [#86](https://github.com/vjmichael/Single-galaxy/pull/86). #86 was retargeted directly to the previously active territorial base PR #77. **Nothing merged into main and no native pass inferred from closure.**
2. **Tasks 1–3 combined into a single CI job:** the existing `.github/workflows/ship-status02-authoritative.yml` has been repurposed with the internal title **SHIP-STATUS consolidated native + live-game acceptance (01–07E)**, one `native_and_live_game` Windows/Linux matrix job, cancellation of stale runs for the same PR, native CMake/CTest and SQLite two-owner evidence, authenticated relay and Linux Chromium **against the source-built game**. The job also compiles the subsequent 07F and COMBAT-01A native tests.
3. **Task 4 explicitly deferred until 3D.** No Windows NSIS/MSI build, Windows installed-game cycle, Windows browser-as-native-game surrogate, or fake packaged acceptance. Windows **native executable compilation and C++ regression** still run in CI. A future Windows installed/3D acceptance gate reopens once original 3D geometry/assets and true native 3D game presentation exist.
4. Continue **tasks 5, 6, 7, 9 and 10** as separately identified code/test milestones without waiting on queued CI. Missing functionality is not marked verified or complete.

## 5 + 9: NPC hailing and proportionate communication

`include/galaxy/NpcHailSupervisor.hpp` and the existing single original `src/colony_live_host.cpp` now share `ShipHailJournal`, `PersistentWorld`, actual named workforce and `ColonyConflict` governance.

- Only **genuinely existing foreign colony ships** with their real owner, original effective governance and an actual duty-eligible Ship-role leader can reply automatically. The *local authenticated human owner's* hail choice is never automated. This is safe for the current **one-human-owner** desktop host; a genuine multiplayer host needs an explicit player/NPC-control registry before using this assumption across other human accounts.
- Real cooperative leadership may **Identify**; isolationist/expansionist leaders may **Decline** without revealing a controller; predatory leadership remains **Silent**. Empty, absent, off-duty or nonexistent NPC leaders cannot forge an answer.
- A separate bounded NPC initiation path can initiate **one authentic call at most per 50-tick opportunity** only after a named observer records an original physical contact and dated original political disposition chooses **RequestAid** or a territorial **Warn** inside a real claimed region. It does not hail every visible stranger, identify via passive sensing or spawn extra ships. Earlier explicit identification/refusal prevents redundant automatically initiated hails.
- Incoming player–NPC response, actual original SQLite evidence, no extra crew/material and restart are covered by `tests/SHIPSTATUS07F_NpcHailSupervisorTests.cpp`. **Actual native execution pending.**
- Remaining **task 5**: authenticated true multiplayer two-sided owner registry/sessions, cross-account stream scoping, disconnected/reconnected delivery, autonomous NPC choice coverage under real high-level gameplay. Current single-home-owner browser is not a finished multiplayer system.
- Remaining **task 9**: real NPC investigation/response policies based on harassment reports with independent existing dispatch authorization; no magical law fleet.

## 6: credible complaint review without invented law enforcement

`include/galaxy/ShipCommsReview.hpp` checks an original `ShipHailJournal` detailed/compact complaint against a truly authorized victim-owner, the physical colony owner's original territorial claim and a real, duty-eligible named reviewing NPC. It yields explicit stages `MissingEvidence`, `WrongOwner`, `NoClaim`, `NoDutyReviewer` or `AwaitingLawAgency`.

**Not legal adjudication:** even if an original complaint has three or more verified nuisance points, a colony's real ship, territorial claim and on-duty watchperson are **not automatically police officers or judges**. The result therefore never permits criminal guilt, seizure, damage, detention, arrest or an automatic law-enforcement intercept. There is no actual registered police organization in the starting game.

The owner-scoped native security desk emits `SECURITY_CALL_REVIEW` tied to the original complaint event; the browser displays whether a named internal reviewer has examined the credible case and why actual law-enforcement action awaits institutional infrastructure. The UI says *not a conviction*, and uses text content (not HTML injection).

**Remaining task 6:** real law-agency creation/staffing, jurisdiction, reviewer/warrant authority, decision/appeal/correction records, nonviolent original NPC response when actually authorized, and no automatic force.

## 7: security hardening and regression

- The relay's previously malformed `colonySecurityPatrol` condition was **repaired**. The current workflow includes native `python -m py_compile` and exact-shape authenticated relay regression so syntax and wrong-owner input errors fail the gate.
- The existing `colonyFireTraining` action was restored to the explicit colony-mode allowlist; the new combat command `colonyFireShip` is bound to the same verified principal, original piloted ship, and exact `{token,action,target}` schema. The client cannot supply the damage, ammunition, actor owner, warrant or target sensor validity.
- Original `ShipHailJournal` has exact signed-63-bit identity receipts, first-ten immutable detail entries per owner/recipient and compact monotonic later penalties, with original owner-specific totals across recipients; no fake crime or caller ID permitted.
- The user-facing security desk's baseline 07D and expanded 07E JavaScript parsers/DOM suites **passed actual direct V8 execution with Node/DOM compatibility shims** after review readiness was added. This is NOT a C++ compile, real Python relay run, Playwright install or full game test.
- Native 07E regression now demands wrong-owner/review denial, authentic duty and territorial claim before review, and absence of automatic police powers. **Native CTest remains pending.**

## 10: native physical ship-to-ship combat, phase 01A

The original `Simulation::Action` gains `FireShipWeapon` and canonical `galaxy-kernel-v104` snapshot/restore support. `src/colony_live_host.cpp` accepts owner-bound `FIRE_SHIP|target`; the authenticated Python relay admits `colonyFireShip` with a verified numeric target and no client-controlled damage.

- It consumes the **existing finite original ship weapon charges**, shares the original cooldown and makes no infinite ammunition, weapons or NPC fleets. All physical targeting is recomputed at original native execution time: actual separately owned foreign ship, local frame, observed sensor envelope (stealth included), distance, native heading/aim cone, dock/jump state and ammunition.
- It subtracts **real hull hit points** from a foreign ship and records actual damage in the native event, then converts a **physically unoccupied, non-training ship** to a persistent cargo/ownership-preserving wreck at zero hull using original activity and ship-frame cleanup. Ownership and cargo are not magically deleted or instantly lootable.
- **Safety and continuity constraint:** an actual staffed colony ship or any ship with real named people/dependents aboard may only be reduced to a minimum of one hull point. This phase **does not kill, strand or transfer existing people** because the permanent, atomic injury/rescue/custody/loss protocol for crewed hostile ship destruction is not implemented. The condition must not be bypassed until that protocol is independently verified.
- `tests/COMBAT01A_RealShipWeaponTests.cpp` exercises real yaw, command-level self-attack denial, finite ammunition, physical uncrewed wrecks, protected staffed colony ship, cooldown and original v104 SQLite save/restart. Registered in CMake and the unified Linux/Windows native matrix; **execution/passing not yet established**.
- **Remaining task 10:** mature ship weapon manufacturing/fitting/ammunition supply, actual crewed ship disabling/rescue/custody (then lawful lethal consequences), NPC combat policy/physical maneuvers, salvage ownership/disputes, station/system law jurisdiction, real 3D aiming/VFX, and direct installed 3D game acceptance after assets. This native phase is not presented as a completed lethal-battle system.

## Gates still open

| Gate | Current assessment |
|---|---|
| Old duplicate PR review stack | **Complete** — #78–#85 superseded/closed, #86 consolidated open draft |
| Tasks 1+2+3 unified CI definition | **Implemented** — one matrix, native Windows/Linux + Linux source-built game browser, extended 07F/COMBAT-01A |
| Exact commit Windows/Linux CMake/CTest, Python authenticated host | **Not yet independently passed** |
| Actual browser-played Linux local game end to end | **Not yet independently passed** |
| Windows packaged installed game / Windows browser substitute | **Deferred until real 3D assets/UI** |
| NPC hailing behavior | Native initial supervisor coded, test pending; multiplayer and complaint-driven NPC reactions remain |
| Legal complaint triage | Native original-evidence review preflight coded; actual agencies/adjudication remain |
| 3D physical combat | Native physical hull/wreck phase coded; 3D rendering and true crewed-battle custody remain |

**Never treat open-world death, physical custody, damages or legal guilt as mere text or visual effects.** The original simulation and native journals must remain authoritative. Keep code and test evidence separate in follow-up ledger updates.
