# PLAY-13B — Scanning, durable discoveries, and exploration reuse
2026-11-04 · branch `integration/play13b-scanner-knowledge-20261104` · draft PR #46

## User gameplay rules
- Exploration should emphasize selecting a location/signature, adjusting scan radius and deciding what to investigate; never mandatory manual arrangement of eight probes.
- Three scan modes: environmental, celestial and anomalies. A shared native scan pipeline distinguishes categories.
- A survey drone records **data**, not physical ore. Geological drone telemetry stays within 10 km. A deep-space probe has a 0.5 AU normal-link budget.
- Connected deep-space reports use **10 seconds of gameplay response** after capture, **not** four minutes of light-time delay. The game is not a literal telecommunications simulator.
- Discoveries persist and can inform future navigation, colony surveys and hyperlane anchor planning. Stored information is historical evidence, not an automatic declaration that the feature still exists.

## Implemented in this branch (native research/engine foundation)
1. `SurveySensors::scan_mode`: Environmental / Celestial / Anomaly filtering of actual B6 probe observations, preserving owner scope.
2. `coarse_signatures`: returns stable signature IDs, approximate quantized locations and uncertainty without disclosing class or exact position. Lower sensor strength cannot detect very hard signatures.
3. `plan_refinement`: computes deterministic waypoints for *deployed owner-owned probes*, requiring an in-range detectable signature; uses coarse center, not exact world coordinates.
4. `advance_refinement`: B6 native laboratory planner moves probes in finite bounded physical steps. In addition, PLAY-13B v69 now has a separate real-world authenticated `FocusSurveyProbe` / `CancelSurveyFocus` lifecycle, durable focused waypoint, and host-tick probe movement. The two models must eventually converge; the live implementation does not use the older B6 planner.
5. `SurveyArchive::known_site/known_sites`: reusable private SQLite owner-scoped knowledge API.
6. `SurveyArchive::queue_probe_report/deliver_probe_reports`: durable B6 archive outbox with ten-second release threshold, 0.5 AU range gate, no delivery out of range, owner isolation and idempotent retry. Additionally, **v68 native `Simulation` now queues actual PLAY-13 stationary-probe scans as withheld observations**, releases them on scheduled committed ticks with real link checks, and serializes pending records across world restart. `PersistentWorld::configure_probe_report_tick_ms` journals the host tick cadence, and `colony_live_host.cpp` uses actual `TICK_MS` for ten-second timing. No early native discovery events. Native journal capacity cannot discard pending data. The two outbox mechanisms currently serve different layers; consolidate their provenance and exactly-once handoff before final acceptance.
7. `SurveyArchive::import_native_discoveries`: trusted bridge importing v67 owner-specific asteroid discoveries, including geological taxonomy, into the existing durable B6 survey archive. Reimports are idempotent. The actual colony host now creates a sidecar index beside its save, reconstructs it at startup, and syncs after durable native world ticks. The world journal stays authoritative if the sidecar fails.
8. `Astronomy.hpp`: deterministic seeded system archetypes with single/wide binary/circumbinary/hierarchical triple star structures; ordinary/oblate/tidally distorted star morphology, ordered orbital bands, and diverse scientific and fictional features. Generation weights are game-design choices, not astronomical occurrence estimates. **No live orbital/world rendering integration yet**.
9. `GalaxyMap::astronomy`: consumes generator for registered systems.
10. `SurveyArchive::actionable_sites`: revalidates owner-private observations against current map for navigation/settlement-survey suggestions, with minimum confidence and maximum age. It does NOT authorize travel, site construction or extraction.
11. `viewer/scanner-session.js`: tested browser state model for mode selection, range, and locking only host-provided signatures. Live `viewer/interactive.html` and `interactive.js` now expose anomaly search (selectable 5/50/100/500 km host-enforced ranges), signature dropdown, physical probe focus/cancel, and native mission progress. Celestial/environmental *UI modes remain labeled previews* until actual system-integrated evidence is available.

## Cross-platform tests
- `GeographyScanningTests.cpp`: signal strength, owner isolation, coarse targeting, denied owner/range, multi-probe geometry, bounded step movement, real subsequent sensor refinement; SQLite report delay/unique delivery/visibility; deterministic star configurations; recent knowledge filtering, persistence.
- `PLAY13_GeologicalDroneTests.cpp`: live v67 drone data imported into owner-private SQLite archive, preserving taxonomy and original no-extraction/mass invariants.
- `PLAY12_ProbeLifecycleTests.cpp`: updated for v68 deferred discovery, persistent pending reports, native snapshot tamper rejection, cadence change not accelerating queued reports, and radial/diagonal 0.5 AU link boundaries.
- `PLAY13B_LiveSurveyIndexSmoke.py`: real colony native host startup/restart verifies the derived survey index sidecar. Does not yet exercise full lock-on UI.
- `PLAY13B_ScannerSessionTests.js`: node-browser contract for safe signature selection, radius, mode changes, and rejection of unknown IDs.
- `.github/workflows/play13b.yml`: Windows/Linux CMake build + CTest of the two native targets, real host build, plus Linux live-host restart smoke. The inherited malformed PLAY-12 workflow was also repaired on this stacked branch.
- Earlier malformed CTest workflow expression was corrected at commit `967c701a85444af9e76fe5a0c1254dc9db6d1ddb`. Await **current-head success** before passing a validation gate.

## Gates still open — must not be marked complete
A. **Native v69 prototype implemented, acceptance pending.** Authenticated colony relay invokes host-owned `SIGNATURES`, `PROBE_FOCUS`, `PROBE_CANCEL`. The core verifies a fitted exploration scanner, real owner, deployed equipment, 0.5 AU link, and a currently detectable signature. A 5 km coarse position leads to an autonomous physical approach, then the probe must locally detect the real target before refining to a 25 m position and recording a ten-second delayed observation. Precise hidden coordinates are never accepted from the browser. World action, snapshot recovery, browser and integration tests have been added; full-playable acceptance remains open.
B. **Partially implemented.** v69 persists the active target and coarse/fine waypoints in the authoritative snapshot; restores independently check waypoints and ownership, and cancellation stops mission movement. v68 report pending state remains durable. Still open: explicit destruction/abandonment policy, mission retry if a target vanishes, additional failure-injection tests, and full transition/lock-on UI acceptance.
C. Extend cosmic coordinates from current ±1000 km world bounds to meaningful interplanetary scales; verify dimensional units and 0.5 AU range boundaries in the live world. The queued report adapter accepts AU-scale **distance argument** but the actual gameplay positions are not AU-scale.
D. Connect astronomical profiles to native placement/physics-enough geometry, astronomy-driven environmental conditions, resource surveys and visual assets, with no false scientific claims.
E. **Partially implemented.** Browser controls offer anomaly scan, host-enforced range choice, coarse contact selection, focus/cancel and native mission feedback. Environmental/celestial modes are honest preview options, not active authoritative scans. Still open: direct map-click target selection, full three-mode observations, richer uncertainty/progress, replay/retarget behavior and actual desktop gameplay acceptance.
F. Use durable discoveries in live navigation/settlement/hyperlane-authoring pathways with explicit world-state revalidation and authority. Current `actionable_sites` returns *recommendations only*.
G. Pass native cross-platform current-head CI and integrated restart/authorization/corruption/telemetry tests before marking PR #46 ready for merge. GitHub Actions has a massive queued backlog (hundreds of workflow runs triggered by broad PR scopes); lack of a run result is not a pass. Keep #46 draft.

## Data semantics
- `ScanObservation`: source player/system/site, estimated position, observation tick, confidence and publication state. A record's existence never implies continuing resource availability.
- The archive does not silently publish private information or let one player claim another player's report.
- Survey reports cannot be treated as mineral extraction or transferable physical cargo.
- Future entries should retain evidence provenance and observation history. Do not replace verified scientific phenomena with gameplay fiction without labeling it.

Dependency: PLAY-13 draft PR #44, not merged. PLAY-13B PR #46 is stacked on the PLAY-13 branch.

## Native v68 change audit (2026-11-04)
- Stationary `ScanSurveyProbe` no longer writes directly into `Simulation::discoveries_` on command completion. It stores `PendingProbeDiscovery` (trusted observation + owner carrier + probe ID + scheduled release tick), while command results report queued count.
- `galaxy-kernel-v68` snapshots include `probelatency` and `pendingprobe` entries with strict owner, carrier, field and temporal validation. Pending records survive recover-copy and world restart. Old v67 and earlier snapshots remain readable by the existing version dispatch.
- Native link check now uses radial distance rather than a square radius; exact 0.5AU and diagonal checks are regression-tested. Native world coordinate bounds are still only ±1,000km, so AU-scale deployment cannot yet be exercised end-to-end.
- The gameplay ten-second response is a **minimum release delay after a scan**; actual discovery can arrive later if the probe is disconnected. Geology drones preserve their 10km return-to-carrier telemetry rules.
- Live UI point-click lock-on and true delayed probe flight still require native command wiring and end-to-end acceptance. Do not claim these are playable yet.
- The C14 unrelated workflows are not part of this repository; no unrelated project scope should be merged into this branch.

## v69 focus mission milestone
- The native host now emits owner-private `SIGNATURE_CONTACT` records from a fitted scanner; unknown/exact coordinates are withheld, and scan range (5–500 km) is enforced server-side.
- `FocusSurveyProbe` / `CancelSurveyFocus` actions use existing native authorization and durable command journal. The probe performs fixed-radial-budget autonomous movement, including a two-stage coarse-to-local refinement. It never extracts ore.
- `galaxy-kernel-v69` snapshots persist `focus_site` and `focus_waypoint` beside native survey probes. Restore checks observed target identity, approximate waypoint derivation and physical custody. `PLAY12_ProbeLifecycleTests.cpp` covers cross-restart movement, ownership, cancellation and delayed observations.
- **Important distinction:** This is a native 2-D world prototype within ±1000 km. Full galaxy-scale coordinate transitions, environmental and celestial live scan modes, astronomy-backed visuals and acceptance of actual installed gameplay remain open.
