# PLAY-13 — Autonomous Geological Survey and Drilling Drones
**2026-11-04 · branch `integration/play13-geological-survey-drones-20261104`**

## User-driven objective
Extend PLAY-12 physical probes with genuinely autonomous geological survey drones, not a client-side visual that teleports to rocks or fabricates mineral reports.

## First implemented gameplay slice
- The starter ship has a finite probe inventory and a fitted probe-launcher hardpoint inherited from PLAY-12. A **geological drone** uses one of those existing recoverable survey probe chassis. Launching decreases onboard inventory by one. It does **not** generate a replacement drone.
- The player first scans nearby space, selects a geological asteroid in the native geology roster, and issues an authenticated `colonyDroneLaunch` request. The native action `LaunchGeologicalDrone` rejects nonexistent/non-geological/depleted targets, unauthorized carriers, wrong systems, missing launchers, insufficient probe inventory, and targets farther than 50,000 mm.
- A v66 persisted `DeployedSurveyProbe` record tracks drone phase, target, launch/home position, current position, observed geology taxonomy, and physical 1-tonne ore sample. The drone travels at a bounded maximum of **2,000 mm per axis per committed world tick**; it cannot instantly appear at the asteroid.
- When it reaches a still-existing, same-system, non-depleted geological asteroid, the native game transfers exactly **one actual tonne** from that asteroid's ore into drone sample custody, records its real geological class, and adds an owner-scoped first-discovery record when possible. It does **not** extract ore from a rock that disappeared or became depleted.
- The drone turns around and returns to its original launch coordinates, maintaining its sample in physical custody. Only after it has arrived may the carrier move within 2,500 mm and recover the same chassis via PLAY-12's native `RecoverSurveyProbe`. Recovery atomically transfers the real sample to actual ship cargo, subject to free cargo volume, and returns exactly one onboard probe unit.
- Native `total_ore()` accounts for the drone's material and its actual sample separately; sampling and return cannot duplicate ore. The world persists in canonical v66 with strict validation. The host publishes actual phase/target/sample status, and the browser accepts only the committed native roster.

## Boundaries / follow-on
This is a **single-class asteroid drill and return mission**. It does not yet drive to planets or moons, operate underwater or in atmospheres, extract ore from rare gas/ice, navigate around obstacles, model battery/thermal/radiation/wear, or use a standalone expensive drone hull produced through a new fabrication recipe. Those are separate future upgrades.

The first mission samples up to 1 tonne as a gameplay-scale demonstration rather than a realistic gram-scale assay; sample mass/capacity should be revisited in the full geology and industrial material catalog. The drone uses probe inventory as its physical chassis, not a free unlimited ship skill. The initial two hardpoints are still occupied by an exploration scanner and probe launcher. Remote communication-distance restrictions, drone destruction, collisions and hostile interception will require actual systems.

Fold Gates remain **point-to-point transport**, not tools for revealing intermediate space. Small, medium and large one-way hyperlane accelerators are intended as **explorable highways** with voluntary exits and independently constructed reverse directions, not instant jumps; this milestone does not implement them.

## Verification
- `tests/PLAY13_GeologicalDroneTests.cpp`: native owner/target/resource guards, incremental travel, one tonne physical extraction, discovery, persistent mid-mission reload, complete return, recovery transfer and recovered-world copy conservation.
- `tests/PLAY13_DroneWiringTests.js`: strict v66 drone phase, geological target, sample and owner-authorized relay command contract.
- `.github/workflows/play13.yml`: build and run PLAY-09–13 native regressions, browser contracts and parse checks.

**Status:** committed to draft PR; successful exact-head Linux CI and actual physical Windows/Chromium host acceptance remain separate gates. Do not claim complete or production-ready without observed results.
