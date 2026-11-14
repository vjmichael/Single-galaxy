# SHIP-STATUS-07G — Authenticated two-human direct ship communications

**Status 2026-11-06: code and test sources committed on [draft integration PR #86](https://github.com/vjmichael/Single-galaxy/pull/86). Native Windows/Linux CMake, original SQLite and real host/relay acceptance await executed CI steps. No claim of installed 3D game or released multiplayer.**

## What is implemented

One existing original C++ `PersistentWorld` remains the authoritative ship, sensor, owner, crew, module, political and SQLite writer. Previously the local demo allowed only owner 7 to command the native host and to stream its game. The accounts file already allowed several users, but other users could not receive or answer their own hails. This change adds **security-only authenticated two-sided communications** for other human accounts that **already have actual physical colony ships in the world**:

- The trusted relay resolves a signed-in account to its manifest `playerId`, then privately passes a validated roster of human-controlled account IDs to the original host. The host rechecks the real owner of the **lowest-ID existing owned colony ship** before allowing a direct hail, a reply or a security refresh. A second user's HTTP request has **no owner/ship/force/observer override**. If that account has no existing ship, the host returns `SECURITY_ERROR|NO_OWNED_COLONY`; it **never creates a second free colony ship**. Multi-ship selection remains later work.
- **Only** `SECURITY_REFRESH`, `SECURITY_CALL|real_track`, and `SECURITY_CALL_REPLY|existing_receipt|IDENTIFY/DECLINE` are permitted for secondary humans. The original relay checks exact JSON shapes, authenticated cookie/CSRF and same-origin; a second native check denies other actions even if the relay were to forward one. Player 7 retains normal existing gameplay. Non-owner ship controls, combat, territorial claims, warrants, NPC staffing and cargo operations remain disallowed to secondary sessions.
- The original host produces owner-addressed **`PRIVATE|owner|COLONY|1|...`** security output on its trusted local stderr, not on SGVIEW/stdout or the shared colony broadcast. The relay only accepts strict numeric owner prefixes and canonical `SECURITY_...` frames for registered human accounts, strips the envelope and routes them to that account's own private SSE connection. Secondary players receive **no public game frames, survey events or owner 7's ordinary diagnostic stream**. They are not falsely shown an omniscient world map.
- `/viewer/security-only.html` and `security-only.js` provide login/logout, physically detected direct ship hails, pending incoming calls, Identify/Decline, nuisance points, verified review status and reconnect with no CLI. The interface reuses the existing strict `security-desk.js` parser and DOM, but never exposes territorial or combat controls to a secondary human. State clears on logout.
- On an actual hail/reply attempt the trusted host generates fresh, owner-scoped native snapshots for authenticated human users; no case, contact or private inbox is copied across owners. Original SQLite remains authoritative. `SECURITY_REFRESH` was repaired to include the communications journal, so recipients can recover unanswered calls after an SSE disconnect/reconnect or process restart, provided the call remains within its original in-game response window.
- The native NPC hailing supervisor now receives the authenticated human-owner roster and **excludes all of those accounts**. Having named NPC crew or an NPC-style colony governance record does not authorize the server to answer for a human pilot.
- Owner-private SSE uses a bounded 4,096-entry **rolling sequence buffer**, rather than freezing forever when its maximum is reached. SQLite-backed refresh recovers current pending calls; historical dropped telemetry is not misrepresented as durable case evidence.

## Security and anonymity

Silent and declined recipients remain anonymous to their caller, regardless of whether their hull is optically detected. A voluntary **Identify** discloses only the current verified owner; existing anonymous historical case records do not automatically turn into criminal convictions. Once genuinely identified, repeated unsolicited hails accumulate penalties (first ten detailed receipts, later compact monotonic totals) and are blocked. Legal action still requires an actual institution and separate authority. No NPC force, police fleet or arrest is spawned by the transport.

Secondary-human reads use the original owner-filtered `security_desk::snapshot` and ship-sensor visibility checks. A raw `PRIVATE|owner|` line must **never** be shown to the browser, and owner 7's original game frames must **never** be served to another signed-in account. A forged client-side owner, ship, damage number, counterparty identity, or receipt does not become native authority.

## Verification plan

| Gate | Implementation / evidence |
|---|---|
| Original two-owner physical hull + SQLite case and direct-hail recovery | `tests/SHIPSTATUS07G_TwoHumanSecurityTests.cpp` / `galaxy_ship_status07g_two_human` CTest, compile/execution pending |
| Real two-owner account login, same original host, owner8 no SGVIEW, call → private inbox → Identify → penalty, forged commands denied | `tests/SHIPSTATUS07G_LiveTwoHumanHostTests.py`, uses `--seed` native fixture only for acceptance; execution pending |
| Strict relay demux with foreign owner/forged telemetry, no main-world SSE, full rolling buffer and principal-bound commands | `tests/SHIPSTATUS07G_PrivateRelayTests.py`, registered in the same CI job; execution pending |
| Original NPC-governance auto-replies never impersonate a human account | Expanded `tests/SHIPSTATUS07F_NpcHailSupervisorTests.cpp`; execution pending |
| Original security-desk parser and new standalone client syntax/DOM | Both 07D and 07E direct V8 tests passed after changes; `security-only.js` passed V8 syntax and all required UI ID checks |
| Native Windows/Linux unified matrix | `.github/workflows/ship-status02-authoritative.yml`; **awaiting runner execution** |
| Windows installed game / browser-native Windows substitute | **Explicitly deferred until native 3D assets and real 3D game UI**, per user direction |

## Deliberate remaining limitations

- The local desktop game still has one primary gameplay owner; secondary humans have **security-only communications**, not full flight, fleet, planetary administration or shared multiplayer gameplay.
- User accounts alone never spawn physical colonies; the test-only original-world fixture seeds owner 8 solely to prove the real transport. Later actual multiplayer joining must provide a legitimate original ship/crew/colony allocation or an existing owned ship via physical game rules, **not a free magically instantiated fleet**.
- No cross-session pilot-switching or multi-ship selection for a human with multiple ships. No NAT, internet matchmaking, remote listen address or public-account server hardening; the local relay is loopback-only.
- The relay still uses a local private host pipe and one trusted process; **not an independently secured internet multiplayer server**. Account management is local and limited to approved credentials.
- Bounded in-memory telemetry is not a permanent event stream. SQLite case and pending-hail replay is authoritative; old transient display frames may be evicted. Real multi-client load, network interruption and installed game acceptance need independent tests.
- Task 6 (actual law agency), task 7 (broader adversarial security/privacy), task 9 (NPC complaint-driven behavior) and task 10 (crew safety / full combat) remain separate next work orders.

**Acceptance rule:** no marking this feature native-accepted until actual CMake/CTest and live-host runner steps complete, failures are repaired, and exact head tested. Do not reinterpret a queued CI job as a pass.


## SHIP-STATUS-07H continuation: no stale replay and bounded overflow repair

New SSE subscribers now take a high-water mark before the native owner-only `SECURITY_REFRESH`; earlier volatile status is never replayed as though it were fresh. A subscriber that falls behind the 4,096-record private ring receives an explicit `resync` control event, and the relay requests another owner-verified SQLite security snapshot. The client clears stale private read models on that event; an unavailable native host yields a failure, never invented case evidence. The deterministic private-relay regression was extended for reconnect, revocation and post-subscription overflow. **Code/test source is committed; actual native/relay CI remains pending.** This is freshness and recovery, not a durable per-recipient historical notification outbox. Details: [07H report](SHIPSTATUS07H_PRIVATE_SSE_RECOVERY_20261106.md).
