# DISPOSITION-03 — Territorial Communications and Enforcement

**Source status (2026-11-06):** implementation and native tests committed to stacked draft; Windows/Linux exact-head CI has not yet passed.

## Why this gate exists

A real colony guard detecting a foreign ship and recording a warning is **not** proof that the visitor received a message. DISPOSITION-02 gave original physical claims, invitations, guard warnings, grace and trespass. DISPOSITION-03 removes the implicit assumption that an issued warning is a delivered warning.

The existing `sg_disposition02` immutable SQLite table and its chained event lineage are reused. No parallel diplomacy, artificial NPC personality, communication SQL store, cargo ledger, fleet, or flight navigator is introduced.

## Communications and delivery

- `TerritoryJournal::warn` publishes a witnessed **issued notice** with original colony, physical ship, owner, watcher, local claim, world tick, generation and requested 2–10000-tick grace. The issuance is not an acknowledgement.
- `pending_notices(world, visiting_owner)` exposes actual active warnings in the player's **owner-scoped inbox read model**. There is no fabricated universal broadcast.
- `respond_to_notice(world, visiting_owner, colony, ship, warning_id, reply_id, refuse)` records an explicit **visitor-origin** `NoticeAcknowledged` or `NoticeRefused` with a unique response ID and authoritative owner/ship validation. The trusted host must authenticate the caller; supplying a `PlayerId` string is not authentication.
- Both an acknowledgement and a refusal prove that the authenticated visitor has seen the warning. The originally requested grace **starts at that response tick**, never at the guard's earlier attempt to send.
- A visitor that never responds remains `AwaitingReceipt`; the host may try to hail, but **must not fabricate a trespass just because time elapsed**. A future authenticated transport delivery proof may become an additional receipt type; DISPOSITION-03 intentionally implements no pretend network or UI delivery.
- The original `TerritorialStanding` exposes `notice_received`, `notice_refused`, reply ID and effective grace time. Historical notice rows remain intact even when a later invitation or amnesty resets active enforcement.
- Every reply cites its exact preceding warning, and its deadline is independently recomputed from the original warning duration and the reply tick. Duplicate replies and forged parent identities are rejected during historical replay. The native trespass verifier now requires a matching authentic reply in the historical chain, not merely an old guard warning.

## Autonomous NPC enforcement without shortcuts

`TerritorialEnforcement.hpp` provides a finite deterministic read model:

`Outside → NeedsWarning → AwaitingReceipt → GracePeriod → MayRecordTrespass → TrespassRecorded → Expelled`

An invitation overrides the current incident; future invitation revocation opens a fresh warning process instead of resurrecting a prior punishment.

`supervise_territorial_contact` is a bounded host-callable executor. It checks the real local claim, current live named NPC observation, actual source ownership and visitor location. It may issue a new witnessed warning or, after authentic response plus grace, commit a true trespass linked to the original warning and reply. It can submit a **movement-only** patrol through the existing `dispatch_territorial_patrol`, `NPC-RESP-01`, and `FLIGHT-03`. It does **not** grant damage, docking, seizure, unlawful pirate captivity, player arrest or a criminal verdict. The host supplies unique original identifiers, cadence and authorization.

The warning/trespass journal and physical patrol remain separate append-only transactions. If a patrol cannot launch, the already-recorded fact is retained; no false flight or automatic weapon effect is invented.

## Native acceptance tests

`galaxy_disposition03_territorial_comms` covers a genuine colony, six named resident NPCs, an actual foreign ship, local fixed claim, guard watch, owner-scoped inbox, wrong-owner response rejection, 12 ticks without acknowledgement, no resulting trespass, a crashed SQLite reply insert and separate reopened observer, authentic refused notice, five fresh ticks of grace, wrong-owner trespass rejection, witnessed trespass, existing qualified NPC patrol dispatch, expulsion, later invitation/amnesty/revocation, immutable history, person/material non-duplication, SQLite restart, and rejection of tampered delivery deadline.

The earlier `DISPOSITION-02` native test is updated to require actual visitor receipt before trespass. CI on Windows and Linux builds the original disposition and territorial tests together.

## Explicit limitations and next gates

1. The native owner-scoped inbox and authenticated-host API are implemented, but **a production UI notification, radio transmission delay, session proof and delivery from a client application are not yet wired**. User acceptance must not be claimed from a test's direct trusted-host invocation.
2. Claims are still bounded fixed local-region discs around colony ships, not mapped interstellar or planetary sovereignty.
3. Autonomous scheduling requires the host to invoke the supervisor on existing event/cadence updates; a background simulation cadence is not added here.
4. A visitor declining to acknowledge cannot be assumed to have received notice. Future communication delivery and retry policies must preserve this distinction.
5. Original combat/ammunition, lawful jurisdiction/warrants, ship disabling, player arrest and pirate detention are separate unfinished authorities. Territorial trespass alone does not confer any of them.
