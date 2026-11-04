# PLAY-13 — Autonomous Geological Survey and Drilling Drones
**2026-11-04 · branch `integration/play13-geological-survey-drones-20261104`**

## User-driven objective
Extend PLAY-12 physical probes with genuinely autonomous geological survey drones, not a client-side visual that teleports to rocks or fabricates mineral reports.

## Corrected native gameplay design — survey data, not ore
- Finite probe/drone chassis and launcher are unchanged: one launch consumes one onboard device and adds a persistent, owner-identified autonomous drone at its real starting position.
- **Geological drones return data only.** At the target, the native simulation reads its real geological taxonomy and persists a report. It does **not** decrement asteroid ore, carry a physical tonne, change ship cargo, or create physical resources.
- A geological drone is a local device with a **10 km (10,000,000 mm) ship-to-device control/telemetry radius**. The launch target is restricted to this local envelope. If its carrier moves farther away mid-mission, the drone continues to execute its already-issued outbound survey and return autonomously; the host withholds live drone telemetry until radio contact resumes.
- A successful mission persists a report in the deployed-device record (taxonomy 1–3, representing the real asteroid type). **Only recovery transfers that result into the v67 owner-scoped, persistent discovery journal**, where the classification survives the device's removal and restart. No real-time geological revelation is permitted while the drone is away from radio range. The report is data, not cargo.
- The drone returns to its launch point over committed world ticks. It is recoverable only after phase 3 and when its carrier enters the physical recovery radius. The same chassis returns to inventory; no ore is transferred.
- The legacy `sample_ore` v66 field remains in the serialized row temporarily for fixed-width draft compatibility but is required to be **zero** in all new worlds and snapshots. This PR has not shipped as an accepted stable save format. Any pre-correction v66 experimental save with real extracted material requires a controlled migration; it is not silently reinterpreted.
- The full mission, state, position, source geological metadata and physical chassis survive SQLite restart/recovery. The v67 discovery format appends an optional verified geological taxonomy to the existing first-observation record. Native authority and idempotent command IDs remain in place.

## Distance and communications model
- **Drones:** 10 km is a good local control range. For gameplay purposes, the maximum distance applies to command/telemetry between carrier and drone; range loss does not erase results or automatically teleport it home. Future signal occlusion, jamming, energy, and retransmission can change link quality.
- **Deep-space probes:** **0.5 AU = 74,798,935.35 km = 74,798,935,350,000 mm** is a sensible initial communications envelope using a normal ship uplink. Even at light speed, this is about **249.5 seconds (~4 minutes 10 seconds) one way**, so orders and reports must eventually use queued/asynchronous transmission with finite bandwidth, relay satellites and delayed receipt. This envelope is now declared in native code, and the existing same-system stationary-probe scan action checks it; however the current simulation's ±1,000 km coordinate bounds make its true maximum unattainable. Planetary/interplanetary enforcement needs a future cosmic-coordinate, time-scale and transmission model.
- Existing `kProbeRecoveryRangeMm=2500` is literally **2.5 metres**, not 2.5 km; the prior prose misstated it. Local `kLocalSurveyRangeMm=50000` is literally **50 metres**, not 50 km. This is a pre-existing units audit to fix separately before presenting the scanner/range UI as physically scaled.

## Boundaries / follow-on
This is a **single-class asteroid drill and return mission**. It does not yet drive to planets or moons, operate underwater or in atmospheres, extract ore from rare gas/ice, navigate around obstacles, model battery/thermal/radiation/wear, or use a standalone expensive drone hull produced through a new fabrication recipe. Those are separate future upgrades.

The first mission samples up to 1 tonne as a gameplay-scale demonstration rather than a realistic gram-scale assay; sample mass/capacity should be revisited in the full geology and industrial material catalog. The drone uses probe inventory as its physical chassis, not a free unlimited ship skill. The initial two hardpoints are still occupied by an exploration scanner and probe launcher. Remote communication-distance restrictions, drone destruction, collisions and hostile interception will require actual systems.

Fold Gates remain **point-to-point transport**, not tools for revealing intermediate space. Small, medium and large one-way hyperlane accelerators are intended as **explorable highways** with voluntary exits and independently constructed reverse directions, not instant jumps; this milestone does not implement them.

## Verification
- Native test `PLAY13_GeologicalDroneTests.cpp` checks owner/target guards, exact distance units, non-destructive geological reading, 0 t extracted, no journal leakage before recovery, persistent taxonomy after recovery and restart, physical device custody and constant world mass.
- Browser `PLAY13_DroneWiringTests.js` checks zero-cargo geological telemetry and authenticated target selection.
- `.github/workflows/play13.yml` builds the integrated native host and runs PLAY-09 to PLAY-13 regression tests. Exact-head CI and Windows/browser tests must pass before the draft is treated as complete.

**Status:** implementation corrected to player direction; verification is not yet proven.\n