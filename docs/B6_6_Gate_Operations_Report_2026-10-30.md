# B6.6 — Durable Stargate Operations: Equipment Condition, Staff & Spares

**Date:** 2026-10-30  
**Project:** Original Single-Galaxy Game / Lumen Reach  
**Status:** Native kernel + live read-only interface milestone; Linux verified. **Not** a completed labor economy or graphical playtest.

## What changed

1. The existing seven-system, nine-link region now supports a persistent `GateOperations` record **per bidirectional connection**. Its fields are condition (0–1000), stocked replacement parts, assigned technician-equivalent headcount, maintenance cycle count, and cumulative parts consumed.
2. Every ten committed simulation ticks, operating gates experience deterministic wear. A staffed gate with spare parts repairs its condition (consuming stock), while an unstaffed gate deteriorates more rapidly. Condition zero automatically shuts **both endpoints**. In-flight jumps already authorized continue under the B6.5 transition contract.
3. Restoring condition does **not** automatically restore the gate to service; reopening requires a separate authorized operation. This prevents an unstaffed/broken link from repeatedly resetting itself to online.
4. The existing region planner uses **committed** gate endpoint status, so failing links affect routes and can create actual chokepoints.
5. `PersistentWorld` writes gate operation state into the same versioned authoritative SQLite snapshot as ships, gates, and cargo. Staffing and spare-part commands use the verified commit/replay journal path. Public `PersistentWorld::submit` rejects maintenance, supply and gate-admin actions even when a client attempts to spoof the reserved server issuer.
6. The seven-system live host starts with explicitly pre-staffed, stocked demonstration gate links. It emits **read-only local-system `GATE_STATUS` records** over the existing validated survey relay, with condition, technician count, spare parts, and operational status. The browser validates the message schema and displays it as sensor information. It does not expose cross-system infrastructure state or maintenance mutation controls.

### Deterministic balancing values for this prototype

| Parameter | Prototype value |
|---|---:|
| Condition range | 0–1000 |
| Maintenance tick interval | 10 logical ticks |
| Operating wear, no technicians | 25 condition/interval |
| Operating wear, one technician | 12 condition/interval |
| Operating wear, two or more technicians | 6 condition/interval |
| Repair when staffed + spare parts available | Up to 20 × technician count / interval |
| Parts consumed by an actual repair | 1 part per maintenance interval |
| Condition reaches zero | Offline at both ends; manual trusted restart after repair |

These are **test constants, not realistic failure rates or final economics**. The model purposely has no random breakdowns yet; failing conditions can be reproduced deterministically in tests.

## Verification

- **25/25 CTest suites passed** (1–14 as a bounded batch, later live tests individually to avoid intermittent live-harness contention). Includes previous B0–B6.5 regression suites and two new B6.6 tests.
- **47/47 Node tests passed**, including the new local gate sensor parser/rejection checks.
- **B6.6 native gate test**: neglect → outage → alternate routing → reject premature restart → authorized staff + parts → repair → explicit restart → original route; rollback injection; successful independent recovery-copy journal replay; restart; snapshot tamper rejection; and jump across a managed gate without dropping maintenance records.
- **B6.6 live host test**: 46 genuine committed client frames, local-system telemetry, nine persisted bidirectional gate operations records, finite repair-parts consumption.
- Focused B6.6 native test passed **AddressSanitizer + UndefinedBehaviorSanitizer**.
- Production-style C++ host compiled with `-DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF`.
- Previous multi-hop, QA disruption, relay, and scan integration tests rerun after live wiring.

### Important defect found and fixed

A B6.5 gate-status command could assign `snapshot_version_=5` to a B6.6 version-6 world, silently dropping new gate operations records from subsequent persistence. Jump handling had a similar assignment. Both paths now use a monotonic version update, with regression coverage for gate restart and actual ship transfer. A sanitizer run first caught the resulting null operation record in the test; it is resolved.

## Security and authority boundary

Test input automation and player autopilot are **different systems**. QA automation remains test-build/runtime opt-in; it is not secretly treated as a cheating signal. In all cases authoritative validation must occur in the trusted host; merely hiding UI controls does not provide security. This prototype's local host is **not a hardened public multiplayer server**. Trusted test/host provisioning functions must not be exposed as public RPC endpoints.

## Deliberately unfinished

- **Actual NPC identity, recruitment, experience, mentorship, deaths, salary and shifts** are not integrated. `technicians` currently represents staffing capacity, not specific persistent NPC entities.
- **Spare parts are not yet physically manufactured or delivered from station/ship inventory.** The test host provides administrative provisioning, explicitly avoiding any claim of a closed resource ledger for gate materials. Next step: physical maintenance inventory and labor contracts, with conservation checks.
- No fuel or electrical-generation economy, gate ownership bills, or player construction process.
- No public player autopilot yet; route assist and the QA harness remain separate.
- No actual Windows-native execution or visual browser-playtest verification was performed in this milestone.
- The model should not claim secure multiplayer authentication or anti-cheat from the local prototype alone.

## Next proposed gate

**B6.7 — Physical Gate Supplies + NPC Employment**: Introduce persistent NPC workforce identities or verifiable aggregate workers, actual station-to-gate shipment inventories, repair parts consumption tracked by conservation ledger, labor mobility and payroll. Understaffing and broken logistics then affect travel without arbitrary scripted gate outages. **Player autopilot** should receive its own design and implementation track, independent from QA input automation.

### Reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 4
ctest --test-dir build --output-on-failure -R 'galaxy_b66'
node --test tests/*.js
```

For the test-enabled interactive host, see `README.md` and previous B6.5 QA report. Never enable QA mode in public games.
