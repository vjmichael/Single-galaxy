# SHIP-STATUS-07E — Authenticated Hail Anti-Harassment and Security Review

**Development status, 2026-11-06:** Implementation committed on a stacked draft branch after SHIP-STATUS-07D (PR #85). **Native Windows/Linux C++ compilation, CTest, native-host relay and installed-app acceptance remain unverified** until a real runner executes those steps. This feature does not implement lethal ship-to-ship weapons.

## Player-facing rule

The first successful direct ship hail delivers a request to the **actual physically sensed recipient**. It identifies the **authenticated caller** to the recipient; it does **not** automatically reveal the recipient's player/NPC controller. The recipient has a real, separate **Identify my ship** or **Decline** response. Silence is also permitted. A prior original authenticated territorial voluntary hail or notice receipt may independently establish that the caller's colony has already learned the same vessel's identity.

After a recipient has **actually identified itself to this caller**, a subsequent unsolicited hail is an abuse attempt:
- The redundant hail is **not delivered**. It receives a durable communications-nuisance penalty tied to the true authenticated **caller account**, real originating hull, real recipient hull and original world tick.
- A maximum of **one scored nuisance event per user/target per world tick** prevents forged rapid request floods from filling the immutable table. Every attempt after identification on later ticks may accrue additional penalties even if the caller is already muted.
- The simulation runs fast: at its normal **25 ms native tick**, these cooldowns correspond approximately to **5 seconds, 20 seconds, 1 minute, and 3 minutes**, respectively. Incoming calls remain answerable for about **60 seconds**; a declined call enforces roughly **30 seconds** of silence. These are simulation ticks, so real elapsed duration depends on the selected tick rate.
- Strike **1** assigns one nuisance point and a **200-tick** outbound direct-hail mute; strike **2** assigns two cumulative points and an **800-tick** mute; strike **3** assigns three cumulative points and a **2,400-tick** mute. Strikes **4+** extend the outbound mute to **7,200 ticks**, **without a ten-point gameplay penalty cap**. Ten is the detailed evidence-row limit *per recipient*, not the penalty maximum. Further distinct-tick attempts continue to accrue points through a compact original-SQLite counter. All restrictions are derived from durable evidence after restart. A mute blocks unsolicited direct calls to **other ships too**, preventing target switching to dodge the penalty.
- On **3+ cumulative points** against an identified recipient, the committed report becomes **eligible for independent law-enforcement/security review**. A real recipient owner may inspect the complaint and the originating verified caller, including the original event receipt. A report is **not** a criminal conviction, automatic warrant or permission to destroy, arrest, detain or confiscate; actual law-enforcement agencies and jurisdictional legal review need a separately established authority system before automatic delivery or sanctions.
- **Repeated unanswered calls** are suppressed while the original request remains pending (at most 2,400 simulation ticks). A recipient's **Decline** keeps their controller undisclosed and suppresses further calls for **1,200 simulation ticks**. Declining is not equated with an authenticated identity or automatic criminal finding.
- Legitimate responses to an incoming territorial warning, a distress workflow or an owner-disclosed reply are **not** outbound unsolicited ship-to-ship hails, so they do not incur this penalty.

## Authoritative provenance and persistence

`include/galaxy/ShipHailJournal.hpp` maintains a bounded immutable `sg_ship_hail07e` table in **the exact original PersistentWorld SQLite save**, with no second physics/character/fleet simulation, created after verifying the original snapshots table. Event kinds: Call, Identify, Decline, Abuse. Every record has a unique native event ID, original world tick/generation, named physical caller/recipient ships, session-derived caller principal, cryptographically *unsigned* linked checksum, and a distinguished identity proof. SQL triggers reject ordinary UPDATE/DELETE; backups through `PersistentWorld::recover_copy` include both original world and hailing journal. A checksum is not protection against a malicious database administrator.

The original `TerritoryJournal` is verified before its **genuine** authenticated voluntary-hail or notice-response event may serve as external prior identity evidence for the same recorded physical colony/hull pair. No passive scanner, internal `Entity.owner_id`, visual resemblance, guessed entity ID, or untrusted client field is accepted as prior proof of identity.

`SecurityDeskHost.hpp` verifies the current true authenticated owner of the selected original piloted ship before `SECURITY_CALL|target` or `SECURITY_CALL_REPLY|originalRequestId|IDENTIFY/DECLINE`. `ShipHailJournal::call` also rechecks the real physical local-frame and the **same actual native stealth/sensor acquisition range**; the caller cannot use a guessed ship ID to reach an unseen target. `reply` only accepts the verified recipient ship's controller within 2,400 world ticks of a genuine unanswered call, never a client-supplied owner/observer/penalty.

`src/colony_live_host.cpp` constructs this evidence journal under the existing single-writer game lease. Startup nonces are raised above all existing original command, territorial and ship-hail receipt IDs, avoiding replay/identifier collisions if a crash follows several same-tick interactions.

## Authenticated transport and interface

The original local relay adds strict CSRF/session-bound `colonySecurityCall` (actual observed ship ID only) and `colonySecurityCallReply` (canonical native signed-63-bit request ID as **string**, identify/decline choice only). Unknown/extra owner IDs, fabricated law or police orders, wrong types, forged account sessions and cross-site requests remain rejected. The existing desktop relay currently exposes only the local home owner (player 7), while the native two-owner acceptance fixture covers the other side.

The in-game Security Desk now displays:
- **Hail observed ship** (initial actual call, visibly disabled when muted)
- **Incoming direct ship calls** with the real authenticated caller ID/ship and Identify/Decline choices
- **Nuisance score / mute-until-tick** plus independently review-eligible complaint receipts visible to their legitimate recipient
- An explicit distinction between an anonymous scan or silent recipient and an authenticated voluntary identification

New wire frames are `SECURITY_CALL_INBOX[_RESET]`, `SECURITY_CALL_PENALTY`, `SECURITY_CALL_SANCTION`, and `SECURITY_CALL_COMPLAINT[S_RESET]`, delivered through the existing authenticated COLONY telemetry, never as an alternative player-controlled combat writer. Browser parser rejects malformed shapes and preserves 63-bit IDs without rounding; private state is erased on logout.

## Host exhaustion and error containment

The harassment ledger holds at most **10,000** immutable original-world events, with at most **10 detailed scored abuse receipts per caller/recipient**. The separately bounded **`sg_ship_hail07e_totals`** table then stores one compact row per offender–recipient pair, updated **atomically by one score point per new offending tick** starting at the eleventh violation. The original first ten entries remain immutable; subsequent attempts still earn points and stay blocked without producing unlimited individual event rows. A separate authenticated **account-wide score sums across different recipients**, rather than taking only the highest target score. The compact total uses a signed-63-bit SQLite integer (the representable data limit, not a game-designed ten-point ceiling). Ordinary SQL cannot decrease or delete the compact count due to monotonic UPDATE and no-DELETE triggers; the compact history retains its ten-receipt seed and latest event, but does not retain a separate immutable proof of *every* later strike. Neither checksum nor SQL triggers defeat a malicious DB administrator. When the overall immutable detailed journal is full, new unrecognized direct hails are suppressed; **already-seeded compact penalty counters can continue to rise without additional detailed entries**, provided aggregate storage remains available. A failure still denies the outbound call without fabricating proof or crashing the physical world. Unexpected communications journal errors emit `SECURITY_ERROR|COMMS_UNAVAILABLE` from the trusted host and do not replace or terminate the original physical simulation; only verified committed entries can be presented to the security desk. Bounded recipient complaint views display the newest eligible reports.

### Ledger-vs-score verification introduced after user feedback

The native 07E regression now exercises detailed abuse points 1 through 10, then points **11 and 12 without increasing the original detail-row count**, checks a second SQLite copy/restart with the higher total, and asserts that direct SQL attempts to lower or delete the compact counter fail. It also checks that account-wide points sum across two different genuinely identified recipient ships. This is **test source written and committed**, not yet a passed C++ run.

The GitHub workflow currently configures the original C++ host on both Windows and Linux, runs native CTest and the authenticated host/relay tests, and launches a real Chromium session against the **source-built Linux host**. It does **not** build an installer, install an NSIS/MSI or run a packaged Windows game. The workflow step has been renamed to distinguish live-browser integration from installed-application acceptance. Windows packaged installation, launch, restart and uninstall remain a separate acceptance requirement.

## Acceptance matrix

| Gate | Status |
|---|---|
| Original hailing, identity response, nuisance escalation, global mute and immutable SQLite schema | **Coded and committed** |
| Native two-owner regression, duplicate same-tick protection, refusal anonymity, identification from direct reply or real territorial record, SQL tamper denial and copied recovery | CMake test `galaxy_ship_status07e_hail_abuse` **written; execution pending** |
| Exact-shape authenticated relay regression, including wrong-owner and forged-authority input rejection | Extended `tests/SHIPSTATUS07D_SecurityRelayTests.py`; execution pending |
| Actual GitHub security desk parser and DOM tests, including 63-bit inbox receipts, blocked repeat calls, visual mute and complaints, logout privacy | **Passed direct V8 test execution** of both older 07D and new 07E suites using Node/DOM shims |
| Real native host/relay and source-launched Chromium game browser smoke | Expanded existing 07D live/Chromium test source; execution pending. **Not a packaged installer acceptance test** |
| Windows/Linux native build, original SQLite acceptance and installed Windows game | **Not verified**; check exact PR head CI runner steps, repair failures |
| Formal law enforcement agency registration, judicial guilt and automatic police dispatch | **Separate future milestone**; no fabricated police exist at genesis |

**2026-11-06 consolidation update:** the earlier PR #85 and preceding #78–#84 were closed as superseded review threads; their code is retained in active draft #86. The user's revised gate defers **Windows installed-game acceptance until native 3D geometry/assets and true 3D game presentation**, but keeps Windows compiler/CTest and Linux live-game testing active. See [the current work ledger](ACTIVE_WORK_AND_ACCEPTANCE_2026-11-06.md) and [next-wave report](SHIPSTATUS_NEXTWAVE_COMMS_REVIEW_COMBAT_20261106.md). Keep PR #86 draft until exact-head native acceptance; do not claim an installed 3D game exists. Reuse the original security agency/governance and NPC flight dispatch if formal law enforcement becomes available; never create an NPC law-enforcement fleet in response to a nuisance report alone.
