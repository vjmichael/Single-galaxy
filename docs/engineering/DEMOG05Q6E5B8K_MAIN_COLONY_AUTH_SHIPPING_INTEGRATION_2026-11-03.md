# DEMOG-05q6e5b8k — Integrated authenticated colony live host and freight status

**Date:** 2026-11-03. **Scope:** Original Single-Galaxy C++17/SQLite main live
colony UI. Not EVEmu. No production multiplayer/Windows certification.

## Integrated code

- The original `galaxy_colony_live_host` accepts an optional
  `--authenticated-transport` mode, **without changing** the original three-arg
  local demo interface. Every command must be prefixed by the trusted relay's
  `AUTH|player-id|...`; native admission independently checks the command's
  owner against the in-game colony ship (player 7 in the starter scenario).
- Native host now locates the player's real existing colony ship when opening
  production-world saves where other structures have earlier entity IDs.
- Both the colony host and standalone native shipping writer use the same
  world-specific exclusive `.host.lock`. There is one authoritative SQLite
  world and exactly one active game writer; a second writer fails closed.
- `live/interactive_relay.py` supports v63 account files (`--accounts`) and a
  read-only native shipping bridge (`--shipping-bridge`). Uses the exact v63
  scrypt parameters, per-player server-held identity, rate-limited logins,
  HttpOnly/SameSite cookies, session expiry, authenticated SSE, same-origin
  command POSTs and a per-session CSRF secret. Browser requests cannot choose
  player/owner ID. The native shipping reader uses `LIST|principal` privately.
- The original `viewer/interactive.html` incorporates sign-in and a shipment
  panel (read-only) rendering physical cargo receipts and settlement from the
  same save. Existing C++ simulation, UI and legacy local relay remain intact.

## Verification and restrictions

- Legacy colony host + relay tests, real freight test and native shipping
  transport regressions pass with the modified host (see testing log).
- Added permanent `galaxy_integrated_colony_freight_session_e2e`: anonymous
  session/SSE/command denial; credential ownership; foreign-player rejection;
  refused spoofed client IDs, CSRF and origin bypasses; authenticated mining,
  refinery, fabrication and physical outpost; native writer collision; restart
  persistence; real four-owner warehouse→carrier→warehouse delivery statuses,
  including alternate destination and role-specific visibility.
- Session and world projection scope remain **one local colony owner**.
  This integration is deliberately **read-only for freight mutations** until
  the canonical game-host command writer accepts the full freight lifecycle
  through this single process. Do not start a second shipping writer against
  an active colony host, and do not interpret client login as WAN-safe auth.

## Browser-rendering acceptance

A headless Chromium/Playwright navigation attempt to the local authenticated
server was **blocked by the execution environment** with
`net::ERR_BLOCKED_BY_ADMINISTRATOR`. This is **not** a passing browser/UI
rendering test. HTTP + native command + SQLite projection tests are passing.

## Next gate

Add in-process freight command execution to the active colony host using its
existing `CanonicalShippingHost` authority, with actual regional state and
immutable receipts. Test collision/recovery and multi-owner role actions;
then expand the single-owner colony view and run native Windows acceptance.
