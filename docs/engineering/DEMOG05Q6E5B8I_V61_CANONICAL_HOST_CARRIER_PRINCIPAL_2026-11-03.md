# DEMOG-05q6e5b8i — v61 canonical host carrier-principal enforcement

**Date:** 2026-11-03. **Status:** Merged-kernel local host authorization hardening. Production multiplayer ingress remains OPEN.

The v60 canonical game/kernel source is now committed on `main` (commit `a363cc99d0bf29802e82c284d396bdf2f218e3a1`). A follow-up audit found two confused-deputy paths in `CanonicalShippingHost`: `load` and `deliver_redirected` checked that a game principal was authenticated, but did not verify that this principal actually owned the assigned carrier. Those host-only methods delegated cargo movements to registry methods without a caller parameter. A different authenticated colony owner could therefore trigger a physically valid but unauthorized carrier operation if they knew the manifest ID.

## v61 changes

- Added `ThirdPartyRegistry::require_carrier(id, principal) const`. It verifies the active host-authenticated principal and the **immutable `sg_v56_contract.carrier_owner`** for that shipment, then checks registry witness consistency. It is a read-only ownership proof, not a separate stock ledger.
- The canonical host now checks `require_carrier` before either cargo `load` or `deliver_redirected`. Original `ThirdPartyRegistry` internal host-only transport operations remain private to trusted orchestration; direct public browser/pipe ingress must never expose them.
- Expanded `V59_multi_destination_host.cpp` with failed attempts to load/deliver by all three other authenticated owners (source owner, original receiver, alternate receiver), while retaining successful carrier-only transfer, exact retry, independent destination ownership, and ledger replay/restart tests.

## Executed evidence

- GCC merged kernel rebuilt and linked successfully.
- Six source/ownership/replay regression CTests: **6/6 PASS**.
- Focused four-owner freight test (GCC Debug + UndefinedBehaviorSanitizer): **1/1 PASS**.
- Full 138-test rerun: **not completed in this turn**. A combined run was interrupted by the environment time limit while executing older slow live/relay E2E tests; it is **not** claimed passing on v61. v60 previously passed 138/138.
- Native Windows, real network session identity provider, and playable GUI freight commands remain untested.

## Next acceptance

Connect a **trusted game-session principal resolver** at the real host transport boundary. Untrusted UI messages must never supply their own principal or invoke registry internals. Enforce lifecycle fencing and per-command authorization across dispatcher/restart; test stale/revoked sessions, duplicate command submission, and all carrier-specific actions. Integrate a read-only manifest status projection into the existing client contract, with no cargo balance duplication.

No dependent fatalities or child-harm depiction; police/fire/institutional EMS deferred.
