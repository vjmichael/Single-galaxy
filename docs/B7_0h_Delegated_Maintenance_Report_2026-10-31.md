# Single Galaxy — B7.0h Engineering Report

**Date:** October 31, 2026  
**Milestone:** Delegated maintenance, finite spare-part logistics, bounded NPC technician assignments, durable colony policy and interactive controls.  
**Status:** Implemented and verified in the bounded 2D Linux simulation. This is neither a production game nor a continuous-3D galaxy.

## What changed

- A player who owns the founding colony ship can physically deliver or recover **manufactured structural modules** as spare parts at their own nearby outpost. One spare module represents **2 tonnes of already-accounted material**. Delivery is not teleportation. The site's parts reserve is limited to 1,000 modules and each transfer to 32 modules.
- Existing **resident colonists** can be assigned to utility maintenance, up to eight technicians and no more than the resident population at the site. Assigning technicians does **not** create new NPCs, transfer population, or require the ship to remain physically present after setup. This is a headcount/job-role proxy, not individual named-NPC task scheduling.
- A locally authorized standing maintenance policy enables the installed utility to repair itself on a 20-tick service cycle when its condition falls to 40 or below, a housed worker has previously maintained local provisions, and at least one spare module is present. The repair consumes the spare module, restores up to 70 condition points, increments an explicit delegated-repair counter and moves its mass into the utility's embodied material record.
- **Resource exhaustion remains meaningful:** no spare parts or technicians means repairs stop, condition eventually reaches zero, and energy-storage exhaustion can interrupt the farm and habitat. The model does not impose any logout tax or summon replacement materials for absent players.
- A new **SQLite snapshot version 14** stores each utility's spare modules, assigned technicians, policy, delegated repair count, and blocked maintenance cycles. Older snapshot formats v1–v13 remain supported. New queued maintenance commands and delivery receipts survive replay and copy recovery.
- Material deliveries are recorded as `utility_spare_transfer_t` in the independent `resource_ledger` (positive for delivery; negative for recovery) and reconciled with command journal records. A tampered receipt triggers recovery failure. Delegated repairs themselves are inferred by deterministic simulation and the authenticated checkpoint, not presently recorded as separate resource-ledger events.
- The native C++ local host emits **`MAINTENANCE`** and **`MAINTENANCE_ASSESS`** typed read-only telemetry. Matrix Vision has controls for spares, technician assignments and delegated-policy enable/disable. Python local relay accepts exactly the new request schemas and enforces session-token, same-origin, action allowlisting, owned-site and near-range checks through the simulation boundary.

## Decision boundary: no automatic prosperity

The new `delegated_maintenance_ready` flag reports site-level maintenance readiness only when installed utilities are powered, supplied with at least one spare, staffed locally by resident technicians with three historic supplied cycles, and configured for delegated maintenance. **It does not certify colony self-sufficiency, expansion readiness, or broad NPC autonomy.** `autonomous_growth_ready` remains **false**. Real quality of life, healthcare, restorative leisure, reserves, profitable surplus, resilient supply chains and responsible NPC administration remain future milestones. This is a key protection against the game awarding growth for subsistence poverty.

## Verification performed

1. **Full Linux CTest:** 41/41 native and integration suites passing with `ctest --test-dir build -j4 --output-on-failure` (includes new B7.0h kernel and live relay suites).
2. **Full Node.js suite:** 67/67 passing with `node --test tests/*.js` (includes three new B7.0h UI/telemetry contract tests).
3. **Production-style Linux compilation:** `cmake -S . -B build-prod -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF` followed by `cmake --build build-prod -j4` passed. This verifies native host construction without test-specific QA features.
4. **Persistence:** B7.0h native test covers starting without infrastructure, physically constructing the outpost/farm/habitat/utility, delivering spares, assigning actual finite residents, depleted-spare repair failure, authorized-departure continuation, owner/range rejection, mass conservation, v14 snapshot equality, copied SQLite recovery and tampered ledger rejection.
5. **Live local interface:** B7.0h integration test starts the actual compiled C++ host, uses real command routes, restarts SQLite, and validates browser-script/HTTP relay assets and rejection paths. **This is not a manual rendered Chromium click-through.**

Reproduction:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure -j4
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

Visit `http://127.0.0.1:8766/viewer/interactive.html`, construct an owned outpost, solar utility and habitat, deliver housing provisions and relocate colonists. The B7.0h section supports delivery of modules, staff assignment and standing maintenance policy. The ship must be close for setup/material delivery; already staffed and supplied structures continue to run without it while the simulation server runs.

## Limits and next gate

- **No server-stopped/offline catch-up** or private-server governance yet. The simulation advances only while its host runs.
- **No individualized NPC identities, wages, tools, competing job schedules, staffing fatigue, skill checks, transport couriers or automated spare manufacturing/procurement.** Technicians are bounded residential job slots and consume no extra crew beyond being residents.
- Physical materials are conserved; **solar output is game-energy units**, not a full irradiance/orbital/chemistry model.
- Existing historical B7.0f colony fixtures without a built utility still retain provisional electricity for backwards compatibility; these do not prove self-sufficiency. Removal requires an explicit save migration and new starting-world design.
- **Linux native tests only**; native Windows execution and actual Chromium graphical interactions not verified. Continuous 3D flight and high-quality graphical presentation remain future work.
- **Proposed B7.0i:** staged service-resilience/colony-life assessment (health, rest, reserve thresholds, useful production surplus), technician competing-work budgets, fail-safe managed-absence policies, and return briefings. Add tests that prevent a settlement from attaining autonomy when it is merely surviving. An additional independent ledger receipt for automatic repairs is recommended for future defense in depth.
