# DEMOG-05q6e5b8j — Trusted player-session ingress and shipment status

**Date:** 2026-11-03. **Target:** Single-Galaxy unified game kernel at v62 source head, not EVEmu.

## Implemented

1. `scripts/shipping_auth.js` and `shipping_accounts.js`: local operator-provisioned accounts with scrypt salt+password hashes, unique player IDs, private `0600` credential file on POSIX, no public signup, no client selection of PlayerId. Sessions are 256-bit random opaque IDs bound server-side to a provisioned player, issued only after a password check and held in memory for 20 minutes. Cookies are HttpOnly, SameSite=Strict, path `/`, deliberately **not** a public-network TLS or SSO solution. Bad logins are rate limited. No passwords in URL/process arguments.
2. `scripts/shipping_live_server.js`: loopback-only HTTP listener with exact Host and Origin policy, finite request size, CSP, session authentication and CSRF protection for mutations. The browser never sends `issuer`, `playerId`, ship identity or SQL. Every native request contains a principal resolved from a server-held session. Native protocol travels over a private child process pipe; command identifiers and roles are allowlisted.
3. `src/shipping_status_bridge.cpp`: read-only projection by default; after an explicit trusted host launch `--writer SEED REGION COLONY FAMILY1,FAMILY2`, reconstructs **one canonical PersistentWorld, production network, ThirdPartyRegistry and CanonicalShippingHost** in a native writer process. All approved freight actions call the existing v62 host; `Writer::active` is scoped to the current authenticated pipe request, reset even on exceptions. The host lease refuses a second shipping writer on the same world; **older game hosts do not honor this lease**, so they must be stopped before standalone writer mode. No direct remote `HostTransferConsignedStock` bypass.
4. `ShipmentStatusView` queries one read-only SQLite transaction after immutable receipt/ledger validation and returns at most 100 participant-visible contracts. A source, carrier, original receiver or fourth-party alternate receiver sees only consignments involving that PlayerId. `phase` is inferred from canonical immutable approvals, dispatches, cargo transfer receipts, return receipts and settlement attestations—not a second inventory tally. Unauthorized owner returns none. Per-user approval/settlement indicators power the player UI.
5. `viewer/shipping.html` / `shipping.js`: login and read-only status cards showing material, quantity, source/destination warehouses, role, movement and settlement. In explicitly enabled writer mode, the existing canonical host processes available booking approval, carrier dispatch, loading, and settlement acknowledgments. UI inserts untrusted strings with `textContent`, not HTML.

## Executed local verification

- Strict native build of new module and program: **pass**.
- Combined merged code full CMake build: **pass**.
- Partitioned complete registered CTest: **115/115 non-live and 25/25 live = 140/140 passed** (two groups; an initial concurrent test-fixture conflict was corrected by distinct run IDs).
- Debug GCC UndefinedBehaviorSanitizer: focused 3/3 passed, including the real four-owner C++ delivery, token/session test, and native HTTP-to-world integration.
- Tests exercise wrong-password login, forged player IDs, wrong Host/Origin, missing/invalid CSRF, logout invalidation, role impersonation, real source/carrier/receiver approvals, forbidden outsider dispatch/loading, successful physical carrier loading, second-writer lease refusal and durable reopen in read-only mode.
- Optional Playwright/Chromium UI smoke **NOT verified**: this execution environment blocked local browser navigation by administrator policy, despite the local HTTP/native integration passing.
- Native Windows build and installed app acceptance: **NOT RUN**.

## Operator runbook (single-user localhost development only)

In a terminal under a trusted host account, provision a real player's credentials (interactive hidden password):

```
node scripts/shipping_accounts.js --file /outside-repo/private-shipping-accounts.json --username pilot7 --player-id 7
```

Start a **read-only**, multi-account shipment status server against an existing authoritative SQLite world:

```
node scripts/shipping_live_server.js --db /path/to/world.sqlite --accounts /outside-repo/private-shipping-accounts.json --bridge /path/to/galaxy_shipping_status_bridge
```

The launcher prints a random loopback port and local `shipping.html` URL. To enable the tested host-side freight actions, only after shutting down other writers:

```
node scripts/shipping_live_server.js --db /path/to/world.sqlite --accounts /outside-repo/private-shipping-accounts.json --bridge /path/to/galaxy_shipping_status_bridge --writer-seed 5153 --region world-return-net --colony home --families east,west
```

`5153`, `world-return-net`, `home`, and `east,west` above are **the test fixture's values only**, not universal production settings. A real world must use its own host configuration.

## Explicit boundaries

- This is a local credential provider bound to the actual native game-host methods; not an external multiplayer identity service or TLS-authenticated Internet-facing game server.
- The original `colony_live_host` / interactive relay have **not** been converted to share this session authority or writer lease. Do not run concurrent writers on the same SQLite world, and do not expose the existing insecure demo relay to untrusted multiplayer users.
- Only third-party v56–v59 contracts are currently in the status projection; older v55 same-owner manifest presentation, pagination, general warehouse trade UI and full production login identity integration remain separate.
- No financial settlement, other-world material creation or shadow cargo transfer. Native Windows and actual browser rendering require their own acceptance tests.
