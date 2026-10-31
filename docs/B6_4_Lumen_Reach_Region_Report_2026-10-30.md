# B6.4 — Lumen Reach Regional Star Network: Engineering Report

**Date:** October 30, 2026  
**Starting point:** B6.3 routes + survey history  
**Implementation:** C++17, SQLite, Python development relay, browser JavaScript  
**Reference:** Original geography; uses no EVE Online game data, maps, meshes, logos or protocol code.

## Region design

Seven original systems and nine bidirectional gate connections. Each connection is modeled by two separate authoritative stargate entities, one in each system, for **18 gates total**.

| ID | System | Security (0–100) | Sovereignty | Connectivity |
|---|---|---:|---|---|
| 1 | Haven | 85 | NPC | 2, 3 |
| 2 | Sable Gap | 30 | Unclaimed | 1, 4, 6 |
| 3 | Lumenford | 88 | NPC | 1, 4 |
| 4 | Glass Meridian | 72 | NPC | 2, 3, 5, 6 |
| 5 | Driftwater | 68 | NPC | 4, 7 |
| 6 | Cinder Cut | 8 | Unclaimed | 2, 4, 7 |
| 7 | Far Lantern | 25 | Player | 5, 6 |

The region is small and **not a reproduction of EVE's regional or systems map**. Every system includes its own physical station and finite test asteroid; the client reveals two major celestial sites on entry. Additional gas/anomaly signals remain hidden until the owner's sensors detect them. These small test quantities are placeholders, not the eventual million-tonne asteroid design.

### Route economics, safety and access

- From Haven to Far Lantern: shortest `1-2-6-7` (3 hops), safest `1-3-4-5-7` (4 hops). Security affects a planned route penalty but neither prevents access nor grants immortality. Sovereignty is independent of security.
- Route computation is server-side via Dijkstra over **operational gate pairs**. The topology is validated against real committed gate entities, and missing or offline reciprocal endpoints invalidate an edge. The region test disables links on a copy of the graph to verify viable alternatives and disconnected destinations; editing gate state from clients is deliberately *not* available yet.
- `ROUTE|destination|S/Q` returns an advisory path, not a jump. `TRAVEL_HOP` applies physical thrust toward the next planned gate; `JUMP_HOP` requests a normal distance-checked transition. Bad/missing legs are rejected. Client cannot forge a gate ID or jump directly to the requested destination.

### Scanning, state and persistence

- `SGVIEW` keeps the verified v1 on-wire contract. A system transition triggers a full destination-scoped snapshot; no entities from previous systems leak into it.
- `SURVEY` remains a separate sensor sideband. Star/planet records are known on entry; hidden sites require real probes. Private survey history remains system-scoped, durable in SQLite, and attributable to the current demo owner.
- Every accepted jump creates a SQLite `jump_ledger` departure and completion. The B6.4 live test checks the *exact* three-leg sequence and 20 ore on every leg. No source-side cargo duplication is permitted.

## Tests and evidence

- Region C++ test: seven systems, nine bidirectional edges, 18 directed gates, initial fixture preservation, locality, shortest/safer route choices, disconnection behavior.
- Multi-hop C++ host/SQLite test: docked start, undock, mine 20 ore, physically travel and jump `1→2→6→7`, exactly three completed persistent jump records with cargo 20 in every row, destination-only SGVIEW frames, local probe scanning in the final system, safe return-route advisory.
- Existing B5/B6 regressions: mining, cargo logistics, local station markets, valid and invalid jumps, crash/restart ledger, private archive, live route relay.
- Browser JavaScript parser tests: named map topology; route and next-leg bounds; malformed sensor events; controls present. No headless Chromium interaction was claimed.

**Result:** `ctest` **20/20** and `node --test tests/*.js` **42/42** passed on Linux, if unchanged from the attached final logs. `docs/B6_4_ctest_output.txt` and `docs/B6_4_node_output.txt` contain the exact run output.

## Caveats / next gates

1. The test world is currently initialized as a **fresh demo session**; durable SQL is validated inside the session, but a reusable saved multi-session world launcher and recovery UI are separate work.
2. Browser rendering still requires direct graphical verification. Windows `.exe` compilation/execution remains unverified.
3. Player-built gates, gate ownership, gate outages and repairs, warp/navigation acceleration, NPC traffic and meaningful resources beyond test ore all remain planned.
4. Route plan state is local to the running host, not persisted. Ship and cargo transfers **are** persisted. Future route-leg revalidation should include mid-flight gate outages and notifications.
5. Autopilot does not make jumps; this is intentional for travel risk and to retain manual navigation. Existing steering is a prototype two-axis controller, not finished Wing Commander-style flight.
