# Single Galaxy — B6.1 Live Environmental Scanning / B6.2a Gate Transition Kernel

**Date:** 2026-10-30  
**Development branch:** local isolated copy of `SingleGalaxy_B5_4_B6_Geography_Scanning`; upstream and EVE/EVEmu unchanged  
**Status:** B6.1 implemented, Linux tested; B6.2a *headless state machine only*, not live world integrated.

## 1. B6.1 deliverables

1. `src/interactive_host.cpp` takes bounded commands `DSCAN|bearing|cone`, `PROBE_DEPLOY`, `PROBE_SCAN`, `PROBE_RECOVER`, `SURVEY_ARCHIVE`, `SURVEY_PUBLISH`. C++ alone decides ship identity (owner 7), its current in-space position, deployed probe IDs, observation timestamp, and sites. No client input provides site IDs, target IDs, or arbitrary deployment coordinates.
2. `GalaxyMap` contains known star/planet (`KNOWN` on entry) and hidden gas-cloud/wormhole sites. Directional scan dynamically reflects the C++ world entities rather than showing a long-depleted asteroid from a frozen map. Probe scans find server-owned hidden sites; one probe gives coarse estimates for weak measurements, and discovered locations can be refined using the B6.0 sensor capabilities.
3. `SurveyArchive` stores private per-owner records in **the running world's SQLite database**. Publishing requires explicit user action; records are not published by scanning. It preserves the source tick of the discovery. The local demo currently deletes its fresh demo database when the relay stops; long-lived save selection is a later milestone.
4. The local Python relay forwards only allowlisted `SURVEY|1|...` host-originated messages as a separate SSE `survey` event, Base64-encoded for transport. SGVIEW v1 remains unchanged and validated independently. When a viewer reconnects, both SGVIEW frames and sensor messages replay from the same running demo instance.
5. `viewer/interactive.html`, `interactive.js`, and new `survey.js` supply controls and a strict client parser; rejected inputs are not converted into host commands. Scanner controls are disabled in the UI while docked, and the host independently enforces this rule.

**Threat model:** This is a **single-owner localhost demonstration**, not secure multiplayer. All attached viewers share one operator's feed and are allowed to see that operator's *private* scans. Do not expose it on a remote interface or treat it as cross-player data authorization. The sideband is diagnostic/presentation telemetry, not a durable game-event journal. The survey observations are durable within the demo database, but probe placement is not yet persisted, nor are probes simulated as physically moving objects.

## 2. B6.2a: isolated gate transition kernel

`include/galaxy/GateTransit.hpp` + `src/GateTransit.cpp` supply a testable server-state machine:

- Per-ship system identity and in-space, jumping, or gate-cloaked phases.
- Owner authority check, explicit operational gate endpoints, system match, fixed 2,500-mm prototype activation radius, weapon-lock gate denial, and rejection of concurrent transition requests.
- Two-step transfer, server-selected arrival position, zeroed velocity, arrival protection for up to five test ticks, early decloaking when movement is requested, and tick monotonicity.
- Gate closures after an accepted jump don't strand the traveler; closures before a jump prevent acceptance.
- **No live integration:** current `Simulation`, `PersistentWorld`, SGVIEW projection and station transport are still per-system and have not been converted to cross-system ownership, cargo persistence, multi-world authority or atomic handoff. The 2,500-mm radius and five-tick cloak are prototype constants, **not EVE's meters or seconds**. Protected status is a state flag; full combat/targeting enforcement awaits future work.

## 3. Verified Linux gates

Build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
```

- **12 / 12 CTest suites passed**, including all prior station, persistence, logistics, geography, and live E2E suites; new B6.1 relay and B6.2a gate tests included.
- **33 / 33 Node tests passed**, including strict survey record parsing and malformed-message rejection.
- B6.1 native E2E: (a) scan attempts denied while docked; (b) dynamic directional contacts found after undocking; (c) no hidden sites without a deployed probe; (d) private gas/wormhole observations persisted; (e) explicit publish updates saved record; (f) probes recovered; (g) SGVIEW still has the required canonical snapshot and deltas.
- B6.1 relay E2E: same-origin, CSRF token, strict JSON shape and bounded range checks; two running viewers and a late viewer replay **identical** world and sensor messages.
- B6.2a headless E2E: near/far, caller ownership, wrong system, offline gates, duplicate requests, transit lifecycle, arrival cloak, movement break, timer expiry, and lock denial.

Evidence: `docs/B6_1_ctest_output.txt`, `docs/B6_1_node_output.txt`, `tests/B61LiveScanningTests.py`, `tests/B61SurveyNodeTests.js`, `tests/GateTransitTests.cpp`.

## 4. Follow-on tasks (not completed)

1. **B6.2b:** integrate gate transitions with durable per-ship system IDs, inter-system authoritative world partition, restart recovery, cargo/hangar continuity and verified double-entry transfer ledger. Refuse partial commits and duplicated entity visibility.
2. **B6.2c:** extend SGVIEW snapshots/system-generation fencing to prevent stale events leaking from the previous system after a jump; expose jump request and transition controls in the browser.
3. **B6.1 enrichment:** physically move/recover probes; persist probe inventories; improve sensor signal modeling and interactive triangulation; unify survey events with the authoritative event journal and per-player publication permissions; expose multi-user survey aggregation with anti-spam verification.
4. **Cross-platform:** test actual Chromium rendering and native Windows builds. **Neither has been verified here.**
