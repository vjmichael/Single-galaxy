# SHIP-STATUS-07I/07J — Durable owner notices and independent colony law-agency referral

**Implementation checkpoint: 2026-11-06.** Source and original-world regressions committed to consolidated [draft PR #86](https://github.com/vjmichael/Single-galaxy/pull/86), stacked on territorial PR #77. The native Windows/Linux compile, SQLite/CTest and authenticated live-game acceptance are **not yet established**. The Windows installed 3D game remains intentionally deferred until actual 3D assets/presentation exist.

## 07I — Transactional notification delivery, not an in-memory replay promise

The live security stream's bounded 4,096-entry process ring is a *transport cache*, not a durable outbox. This implementation adds `sg_ship_notifications07i` in the SAME ORIGINAL SQLite world, via `ShipNotificationOutbox.hpp`.

- `ShipHailJournal::commit` appends separate, owner-bound notice rows for the sender and recipient **in the same `BEGIN IMMEDIATE` / `COMMIT` transaction** as the original call/Identify/Decline/verified abuse receipt. The compact nuisance increment after ten detailed receipts also includes both notices in its *existing original compact-totals transaction*. Failure to enqueue either principal's notice rolls back the original operation. Original `PersistentWorld` remains the sole game/world writer.
- Each durable record has a stable `seq`, originating immutable event ID, event kind, real observed counterpart ship ID, optional disclosed counterpart account, related receipt and world tick. The stored delivery owner is determined only inside the trusted original C++ world from the *actual owner of the physical destination ship at event time*. Unidentified or declined recipients' owners are stored for routing but **never disclosed to the caller**.
- Original `SECURITY_REFRESH` emits up to 32 **unacknowledged** owner-specific `SECURITY_NOTIFICATION` rows in ascending durable sequence. They remain present through disconnection, 4,096-frame transport overflow, process restart and original SQLite `recover_copy` until a separately authenticated native `SECURITY_NOTIFICATION_ACK|seq` commits.
- ACK checks the trusted principal/actual owned home ship before touching the database. Wrong-owner or unknown sequences are denied; repeat ACK by the correct owner is idempotent. SQL triggers prohibit deletion and any change other than `acked:0→1`. The local HTML/JS parser validates exact numeric wire fields, displays the notice, then submits its exact sequence for native acknowledgment. An HTTP 202 means queued, **not** acknowledged, so lost ACKs will replay after a new authenticated session.
- No lost stream tail is fabricated as receipt history. Prior 07H SSE overflow resync still clears transient state and asks the original host for a new authoritative snapshot. The outbox remains authoritative for these security notifications, not the ring.

**Durable kinds currently handled:** outgoing and incoming direct hails, sender/recipient Identify/Decline outcomes, both perspectives on verified repeated-hail sanctions (including compact 11+ increments), and law-agency referral receipts. Generic SGVIEW/sensor refresh frames, unpublished NPC chat, and unrelated territorial/cyber event families do **not** yet have dedicated durable notification producers; their independent original journals still persist authoritative facts. Pre-07I historical hails are **not** retroactively backfilled into the outbox because unverified owner-at-event mapping would be unsafe if an old ship changed ownership. No claim of once-only browser delivery: delivery is **at least once** until authenticated native ACK.

### 07I acceptance sources

- `tests/SHIPSTATUS07I_DurableOutboxTests.cpp` / `galaxy_ship_status07i_outbox`: injected SQLite insert fault **after** the hail receipt insert must roll back both sides; owner 7 cannot ACK owner 8; Identify/Decline never infers undisclosed identity; retry, idempotent ACK, SQL tamper denial, canonical save/restart.
- Existing `tests/SHIPSTATUS07E_HailAbuseTests.cpp` continues checking detailed and compact nuisance-penalty original source semantics, now backed by transactional notice insertion.
- `tests/SHIPSTATUS07G_PrivateRelayTests.py` now exercises secondary native ACK allowlist and refuses admin/agency commands. Existing live two-human/Chromium tests remain in the same unified job.
- `tests/SHIPSTATUS07I07J_SecurityDeskTests.js` exercises the actual security parser, owner notice ACK, replay after reset, and untrusted telemetry denial.

## 07J — Independently authorized colony law agency, not automatic police

`ColonyLawAgency.hpp` adds two additive, guarded original SQLite tables: immutable `sg_colony_law07j` government appointments and immutable `sg_law_referrals07j` complaint referrals. A player cannot declare a case by posting a guessed ID, choose a fake officer or summon police assets.

1. **Government appointment:** an authenticated owner with a real existing colony and verified territorial claim may request `SECURITY_AGENCY_ESTABLISH`. The original native world requires a published durable `GovernanceEpoch`, its real physically present on-duty named leader, and a **different real named on-duty NPC** appointed as the agency officer. The appointment records exact leader/government epoch, colony and staff. A leadership epoch change, reassignment, absence or duty ineligibility makes the old agency inactive until legitimate reappointment. No free NPC, gun, station, fleet or salary resource is fabricated.
2. **Independent evidence intake:** the same real colony owner may request `SECURITY_AGENCY_REFER|original_complaint`. The native agency must still be valid/current, must still hold territorial jurisdiction, and must find a **third** qualified on-duty named NPC reviewer distinct from the appointing leader and police officer. That reviewer uses the existing `ShipCommsReview::assess` against the independently verified original detailed or compact nuisance journal. The compact 11+ proof was tightened to require the original **same caller**, same recipient, and genuine tenth detailed strike. A threshold score or bare claim alone is not enough.
3. If and only if all independent checks pass, one immutable agency evidence-intake referral is filed in the **same SQLite transaction** as its owner-private type-7 notification. Duplicate original complaints are refused. Original stored source receipt and the reviewer/appointed officer remain separately attributable after backup/restart.
4. The owner-facing security desk shows actual appointment, nonviolent referral status and named reviewer/officer; it explicitly warns that a referral **is not** a criminal finding. No automatic sentence, warrant, NPC pursuit, station fabrication, cargo confiscation, arrest or weapon authorization is produced.

This milestone **connects verified nuisance complaints to a real institution with finite staffing**, but intentionally does NOT implement lawful investigation task execution or due-process adjudication. Later law-enforcement response must use the pre-existing physical `NpcFlightOrder`/`NpcResponseDispatch` authorization and original custody systems, not bypass them.

### 07J acceptance sources

- `tests/SHIPSTATUS07J_LawAgencyTests.cpp` / `galaxy_ship_status07j_law_agency`: deny missing government/claim/foreign owner/unknown complaint, appoint real independent personnel, accept only legitimate third-strike evidence with a distinct reviewer, block duplicate case intake, deny cross-owner outbox ACK, verify no new flight/force, SQL immutable triggers and canonical SQLite copy/restart.
- `tests/SHIPSTATUS07I07J_SecurityDeskTests.js`: 63-bit decimal IDs never round through JavaScript Number, source frame schema and owner-native appointment/complaint controls are validated; no fake arrest commands.

The original combined `.github/workflows/ship-status02-authoritative.yml` now includes both new native CTest targets and the real security-desk JavaScript suite. The Windows job remains **native compiler/CTest, not a Windows browser or installed game**; Linux real Chromium runs only against the source-built game. **No acceptance claims may be promoted without executed exact-head evidence.**

## Next independent development

Extend the transactional outbox producer pattern to **other authoritative event journals** (especially witnessed cyber case creation and territorial notices), and add explicitly acknowledged per-owner historical notification browsing if desired. Connect lawful, resource-backed agency investigative tasks with independent authorization and controlled physical NPC navigation before any NPC arrests or coercive responses; adopt due process/appeals independently. 3D graphics and installed native Windows acceptance remain deferred by project direction.


## Continuation / supersession — STATUS-07K/07L

The later user-directed work extends the original 07I/07J implementation without replacing its SQLite outbox, law agency or world writer. [See 07K/07L active engineering specification](SHIPSTATUS07K07L_TERRITORY_CYBER_NOTIFICATIONS_NATIVE_INVESTIGATION_20261106.md) for same-transaction territory/cyber notification producers, legacy 7→15 schema migration, staff-qualified native Patrol investigation admission and source-flight crash/restart reconciliation. The original 07I/07J evidence/authority rules remain mandatory. Native exact-head CI acceptance is still pending; installed Windows 3D remains deferred.
