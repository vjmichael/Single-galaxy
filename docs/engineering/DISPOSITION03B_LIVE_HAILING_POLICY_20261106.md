# DISPOSITION-03B — Player territorial hailing and informed-offense policy

**Status (2026-11-06):** native host-facing adapter, policy tests and CI fixture committed; production client UI/transport and authoritative criminal sentencing integration **not yet wired**.

## Design
Hailing is customary at a real claimed territory, even when an NPC colony's governance is isolationist or hostile. A player preference defaults to automatic **outbound** hail on claim entry, with a manual-hail alternative when disabled. Turning it off never disables the NPC's own warnings or waives law. The trusted game host detects boundary-entry events and must actually dispatch messages through an authenticated session transport; the adapter merely produces an owner-filtered action model. Do not simulate a delivered message or player acknowledgement just because a prompt is visible.

Pending warnings still use DISPOSITION-03's original owner-scoped inbox and immutable SQLite receipts. A player may explicitly acknowledge or refuse via `player_respond_to_hail`; this checks the original physical ship and visitor owner before delegating to `TerritoryJournal::respond_to_notice`. Only the trusted host can authenticate a network session: a caller-provided PlayerId is not a credential. The action must be bound to the authenticated session by the host.

## Optional 1.5× aggravation
The player suggestion is represented as **3/2**, not a floating-point multiplier. A verified crime finding from the original criminal process, against the same owner/ship/colony **strictly after** an authenticated warning response, is necessary before `territorial_hail_aggravation` recommends 1.5×. Mere presence, automatic hailing, an unanswered NPC hail, warning, refusal, or territorial trespass never becomes a criminal conviction. A confirmed refusal *does* prove the player understood the warning, just as an acknowledgment does.

The recommendation does not deliver a punishment. The existing lawful sentencing subsystem must validate jurisdiction, lawful sanctions, offense evidence and applicable cap, and decide whether such an aggravator is permitted. `suggested_sanction_units` supplies capped integer arithmetic, rounding halves upward; it is not applicable to weapons damage, arbitrary custody time, arrests, ship seizures or repeated compounding. One qualifying response creates at most one 3/2 recommendation, never multiplied per notice.

## Acceptance and remaining work
Native acceptance covers opt-out/manual hail availability, authenticated owner scoping, reject wrong owner and reused receipt IDs, time ordering, independent crime verification, wrong jurisdiction/offender rejection and capped 1.5× arithmetic. The CMake native test runs on Windows/Linux when GitHub Actions picks up this branch.

Next integration must hook session-authenticated player command handlers and presentation UI to the existing in-game client, persist the outbound auto-hail preference in player settings, source actual boundary-entry events, use reliable delivery/retry receipts (not UI appearance as proof), and connect *genuine* criminal-case outcomes into the legal severity policy. No client integration or criminal severity enforcement is claimed as complete in this gate.
