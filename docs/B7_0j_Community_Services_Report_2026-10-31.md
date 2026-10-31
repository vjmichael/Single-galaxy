# Single Galaxy — B7.0j Community Services Engineering Report

**Date:** 2026-10-31  
**Status:** Implemented and Linux-tested in the existing deterministic, bounded 2D colony prototype. **Not** a complete clinical care system, independent NPC life simulator, 3D universe, or finished game.

## Engineering intent

B7.0i established a conservative colony resilience diagnosis and forbade falsely declaring autonomous growth ready. B7.0j adds **one actual player-constructed, supplied, staffed quality-of-life facility** with recurring services and shortages. Instead of treating a new building as an instant happiness bonus, a community hall must continually provide real social meals/restorative leisure to everyone at its local occupied site.

### Gameplay and physical inventory

- **Construction:** An authorized colony ship constructs one hall at a nearby outpost **only after a habitat exists**, consuming 2 previously manufactured modules (4 tonnes embodied). The ship must be owned by the requesting player, in the same system, and close enough to physically deliver supplies.
- **Labor:** `AssignCommunityStaff` assigns 0–4 attendants **from existing housed residents**; workers are not spawned. Attendant assignments and utility technicians cannot exceed the site's resident count. Resident recall is rejected if it would strand assigned staff.
- **Supplies:** `TransferCommunityStock` moves local food or water **in kilograms** in either direction. The authorized ship must be nearby. A working hall spends 1 kg each of food and water **per four visitors**, in addition to baseline nutrition and hydration. Material becomes stored local organic waste and wastewater, conserving mass; no free supply conversion or disposal.
- **Finite service capacity:** One attendant supports up to four site residents/farm workers per 20-tick needs cycle. The entire occupied site must be covered before a period counts. Occupied, supplied, comfortable housing and actual local utility power are required. An operating hall adds 1 energy unit/tick to the already-existing finite solar utility load.
- **Reliable operation:** Three consecutive full successful cycles count as evidence of **access to restorative community service**. Brownouts, inadequate staff, absent basic housing comfort or missing consumables break the streak. A facility with no attendants or occupants is idle, not an artificially penalized unstaffed operation.
- **No invented happiness or healthcare:** We do not yet track individual rest recovery, medical diagnoses, treatment materials, clinics, health outcomes or social relationships. A sustained hall is a *measurable service-access proxy*, not proof that NPCs are healthy or universally happy.

### Model and durability

- Introduced `CommunityFacility` as a canonical site-owned record with local stocks/waste, embodied material, attendants, completed periods, consecutive service periods, shortages and visits.
- Added `ConstructCommunity`, `TransferCommunityStock`, and `AssignCommunityStaff` to authoritative server commands. Existing command ordering, ownership, range, player permission, validation, and idempotence protections apply.
- **Snapshot v15** stores community records and accepts pending community commands. The loader continues to read prior v1–v14 worlds; new features never silently backdate historical snapshots.
- The SQLite event/resource journal receives independently validated `community_construction_t` and `community_stock_transfer_kg` receipts. Restore checks event identity, command type, quantities, ticks and source/destination endpoints. Tampering a separately retained delivery receipt fails closed.
- Updated the `total_ore`/`total_material_kg` conservation invariant to include embodied hall mass and local supplied/waste kilograms. No ore-equivalent material is magically created or removed by hall operation.
- Added `CommunityServicesMissing` at blocker bit 11. The resilience check evaluates **every occupied site**, not colony-wide averages; all must have 3 consecutive adequately covered service periods for `restorative_leisure_proven=true`. The original B7.0i first 11 reason bits stay stable.
- Existing `RESILIENCE` telemetry retains the prior fixed field count for older clients. New `COMMUNITY` and `COMMUNITY_ASSESS` frames report site stocks/operation and service coverage independently. Client decoder strictly validates field counts, ranges, internal contradictions, and forbidden growth certification.
- The local Python relay exposes only allowlisted `colonyCommunityBuild`, `colonyCommunityStaff` and `colonyCommunityStock` commands, rejects unrecognized keys, resource names, malformed numbers, wrong Origin and invalid session tokens. The browser does not control world authority.

### Evidence

From the finished local Linux build:

- **45/45 native/integration CTest suites** passed using bounded groups (1–11, 12–23, 24–34, 35–45); the larger early parallel invocation exceeded its execution limit, so these bounded passes are the controlling regression evidence.
- **72/72 Node.js tests** passed; 3 new B7.0j contract tests.
- New B7.0j C++ test proves physically manufactured construction, no construction before housing, finite local deliveries, controlled labor allocation, continuous service and disruption, material conservation, v15 snapshot parsing, SQLite restart, independent recovery copy, forged issuance rejection, invalid resource selector rejection, remote stock delivery rejection and manipulated ledger rejection.
- New live-host/relay test proves v15 host commands, persisted `COMMUNITY`/`COMMUNITY_ASSESS` frames, host restart, local served controls, strict relay Origin/token and input schema rejections. **Not an actual graphical Chromium click-through**.
- **Production-style Linux build with `BUILD_TESTING=OFF`, `SG_QA_AUTOMATION=OFF`: PASS.** Build and grouped-test logs are bundled under `docs/B70j_*` in the source ZIP.
- **Clean-archive reproducibility:** The B7.0j source archive was extracted into an isolated clean directory. SHA-256 validation of all sources passed; newly configured CMake targets rebuilt successfully; both B7.0j native/live-relay suites passed (2/2), as did three B7.0j Node contract tests (3/3). The clean-extract logs are included under `docs/B70j_Clean_*`.

### Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R 'galaxy_b70j_' --output-on-failure
node --test tests/*.js
```

For a production-style build:

```bash
cmake -S . -B build-production -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF
cmake --build build-production -j4
```

### Explicit limitations / following gate

B7.0j provides a real community facility and reliable, physical *service access* signal; **it does not implement a clinic, medical supplies, diagnoses or treatment, healthcare outcomes, individual schedules, or closed-loop food/water/oxygen certification.** `HealthRestUnmodeled`, `WaterRenewalUnverified`, and `GrowthPolicyUnmodeled` remain set; `autonomous_growth_ready` remains **false**, regardless of high community continuity. No NPC autonomous construction, time-skip whilst a private host is stopped, return briefing, or punitive absentee penalty is implemented.

The game remains Linux-tested headless/native, not verified on Windows or by real Chromium clicking, and still uses a bounded 2D physical prototype.

**Recommended B7.0k:** actual healthcare/first-aid materials and restorative activity outcomes with material/energy budgets, short-horizon renewable-water and oxygen throughput accounting and a separate model for work schedules, followed only later by managed-absence and permissioned NPC governance. Preserve the no-punishment-for-absence design.
