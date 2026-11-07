# SHIP-STATUS-07H — Private security SSE continuity and overflow recovery

**2026-11-06 development checkpoint:** implementation and regression sources committed to consolidated draft [PR #86](https://github.com/vjmichael/Single-galaxy/pull/86). Native Linux/Windows, live dual-account and real browser execution remain unverified. Windows packaged/installed-game testing stays deferred until native 3D assets and presentation.

## Contract and repair

The original two-human SHIP-STATUS-07G transport maintains one authoritative C++ `PersistentWorld` and SQLite ship-hail journal. Its owner-addressed `PRIVATE|owner|COLONY|1|...` stderr envelopes remain relay-internal and are never browser payloads. The registered authenticated principal, rather than a client-provided owner, determines the private stream and the native `SECURITY_REFRESH` command.

A new `/events` subscription previously replayed every retained in-memory private message for its owner, then triggered the durable refresh. This could temporarily display a stale previous-session status or a completed/expired notification before current SQLite information arrived. A connected slow subscriber could also fall behind the 4,096-entry shared private ring and silently skip older notifications.

The 07H repair:
1. Under the relay's condition lock, capture the current private sequence **before** submitting a fresh authenticated native `SECURITY_REFRESH`. Stream only records added after that sequence. A new login never replays an older subscription's volatile status.
2. On each read, detect `cursor < oldest_retained_sequence - 1`. Do **not** deliver a suffix of a truncated notification batch as though the owner's read model were complete. Emit an explicit `resync` SSE control event and issue another original owner-scoped `SECURITY_REFRESH` through the same trusted native pipe.
3. The standalone security UI clears its displayed case/contact/inbox state on `resync`; it then accepts freshly verified `SECURITY_...` frames from the native world. This does not change authoritative receipts, pilots, hulls, sensor identity or criminal findings.
4. If native refresh fails, produce `failed` and close rather than inventing reconstructed evidence. Authentication and session expiration are checked for each SSE loop; logged-out cookies cannot continue draining the private stream.

**Known limitation:** This repairs freshness and explicit notification-gap recovery, not durable delivery of every transitory frame. SQLite snapshots still reconstruct **currently pending hails**, durable cyber cases, complaints, sanctions and jurisdictional state. Events not represented by those records and lost from the 4,096-entry process ring cannot be resurrected. A future durable per-recipient outbox with acknowledged cursors would be needed for historical notification replay; this task does not claim that capability.

## Executable source assertions

`tests/SHIPSTATUS07G_PrivateRelayTests.py` now exercises:
- A new owner-8 private SSE stream receives only a newly issued native refresh and never old process-ring messages (even ones addressed to owner 8), owner-7 data, foreign private owner envelopes or SGVIEW.
- An expired session immediately receives a failure event, not security data.
- More than 4,096 private records delivered after subscription trigger `resync`, discard the truncated prefix/suffix, request a second native refresh, deliver only the new owner-8 security snapshot, and terminate cleanly.
- The existing secondary-account native command allowlist remains intact; owner 8 is not granted flight, cargo, combat, territory or police authority.

The original `SHIPSTATUS07G_LiveTwoHumanHostTests.py` and Linux Chromium two-human source-built smoke remain the real-host integration gates, not substitutes for installed Windows game acceptance. The unified workflow already runs this Python regression; actual **execution and any subsequent failure repairs remain open**. No new CI matrix or independent stacked PR is required.

## Security invariants

No guessing identities from sensor tracks. No forced identification on silent/declined direct hails. Harassment score above ten remains monotonic and independent of individual detail receipts. Complaints require independent lawful review; no conviction, arrest, armed response or warrant comes from an SSE frame. Original single-ship finite-crew genesis and one native world writer remain unchanged.
