# Single Galaxy — B6.3 Routes & Survey History

**Date:** 2026-10-30  
**Starting point:** B6.2c live multi-system prototype  
**Status:** Implementation and Linux automated tests pass; browser visual inspection and Windows-native testing remain outstanding.

## Scope and engineering result

B6.3 connects the existing galaxy graph route computation to a live, bounded client request and preserves more than the latest private site observation. The player can request shortest/safer routes to the two current demo systems, review a site's full recorded survey revisions, and see the latest previous observation when revisiting a system. All route calculations happen on the C++ host using actual gate availability; web clients cannot supply arbitrary edges or claim discoveries.

This is **route planning** rather than multi-hop autopilot. The live world still has two systems; "Assist to Gate" physically navigates to the current gate and "Jump Gate" must be requested separately. No unsafe jump shortcut was added.

### Changes

1. **Server-side advisory route planning:** `ROUTE|<dest>|<S|Q>` requests are limited to currently instantiated systems 1 and 2. Policy S minimizes jumps; Q includes a penalty for low-security destinations. The C++ host builds its route graph from the authoritative gate objects and prevents planning across offline gates. The sensor sideband reports `ROUTE` or `ROUTE_NONE` with a bounded path, policy and cost; the main SGVIEW frame contract remains unchanged.
2. **Live browser/relay integration:** Destination and route-policy selectors; route button and displayed advisory; strict same-origin, session-token and argument-schema checks. Browser code validates route topology, bounded values and sensor message syntax. It does not submit a gate identity, ship ID, or player authority.
3. **Append-only survey revisions:** The existing per-player, per-system `observations` snapshot table is retained. An `observation_history` table appends each accepted revision atomically with the latest snapshot, including publication changes. It has a bounded, owner-scoped `history` API (100 records maximum per query). Old observations remain historical records with original ticks.
4. **Persistent knowledge on return:** When the ship re-enters a system, the C++ host transmits the player's latest archived observations labeled with their original observation ticks. An explicit `SURVEY_HISTORY` action returns revisions from that **current** system only. Dynamic contacts aren't fabricated as live from the archive.
5. **Publication state correctness:** Once an observation is publicly released, a later private follow-up scan doesn't revoke it. New history revisions record the **committed effective** publication state, not an outdated incoming flag.
6. **Sensor message compatibility fix:** The browser sensor parser now permits underscores in legitimate enumerated event types such as `HISTORY_DONE`, `ROUTE_NONE` and pre-existing `ARCHIVE_DONE`, while rejecting unknown event types and malformed content. The local Python relay already permitted underscores.

### Reproducibility and acceptance

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j2 --output-on-failure
node --test tests/*.js
```

- **18 / 18 CTest suites passed**, including the new `galaxy_b63_live_routes_history_e2e` and `galaxy_b63_relay_route_controls` tests.
- **38 / 38 JavaScript tests passed**, including three new B6.3 route and history message tests.
- Extended `GeographyScanningTests.cpp` confirms revisions persist across archive reopen, old scans cannot overwrite newer snapshots, personal history stays private, query limits hold, and publication remains monotonic.
- End-to-end host test sends commands based on committed simulation ticks, performs two actual gate jumps, requests shortest and safer route advisories, records discoveries in both systems, and verifies System 1's earlier observations reappear when the ship returns. A SQLite query checks both systems' history records.
- HTTP relay test rejects cross-origin, invalid tokens, extra client-supplied owner/system attributes, invalid destinations, and invalid route policies; checks genuine C++ route/history results reach SSE.
- The existing B6.2c cargo/market/jump journey and previous scanner and station tests remain green.

Raw logs: `docs/B6_3_ctest_output.txt` and `docs/B6_3_node_output.txt`.

### Important limitations

- Only two star systems exist in the live demo; route policies return the same path although costs differ. The existing standalone graph tests still demonstrate alternate route selection and offline-link failover in a larger synthetic topology.
- No active route execution, fleet autopilot, advanced warp, traffic prediction, NPC scanning, public cross-player survey service, or monetary survey payouts yet.
- Survey archive persistence is local SQLite; submitted scan observations come only from the trusted host. The HTTP session token is a **local anti-CSRF control, not secure multiplayer authentication**. This is not a player-permission or server deployment design.
- History is append-only and currently **unbounded on disk**; query results are limited to 100. A future retention/compaction policy should preserve provenance without unlimited storage growth.
- Published confidence is still the early B6.0 prototype's simple model, not a rigorous Bayesian sensor fusion system.
- Stargate input handling stays manual. Gate arrival timing and cloak mechanics are existing prototype abstractions, not a historical EVE replication.
- Actual GUI/Chromium rendering was **not** verified here. All reported tests ran on Linux; native Windows remains unverified.

### Next proposed gate — B6.4

Implement real multi-hop route execution with a graph of at least four systems and multiple paths. Make route progress update after each **committed** jump, auto-recalculate when a gate becomes unavailable, and test route interruption/restart, scanner-system boundaries and cargo conservation. Preserve player control at dangerous transitions.
