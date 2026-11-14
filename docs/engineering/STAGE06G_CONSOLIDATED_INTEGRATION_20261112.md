# S6-G — Consolidated original Stage 6 integration

## 2026-11-13 — Original Linux SGDX planet nine-tile failure and repair

Original exact-head Actions [run #43481325726](https://github.com/vjmichael/Single-galaxy/actions/runs/43481325726) at SHA `8e846901343c3d9c4119da1800c841012964148c` established Linux full original Stage 6 native batch **PASS**; the independent SGDX custody CTest matrix **3/3 PASS**. Its subsequent original-planet supervisor/guardian-restart test failed at `Stage06GInstalledSupervisor.py:101` requiring **nine original source-backed visual tiles**.

A separate local real executable reproduction captured **1 valid tile, center (0,0)** instead of the required 3x3 at +/-64m offsets. Exact root cause: `original_walking_tiles` invokes the authority-limited `planetary::continuous_surface::native_offset` with +/-64000mm, while that unchanged movement helper refuses offsets beyond +/-32000mm. The eight outer visual tiles are silently omitted despite valid underlying source land.

Repair: `Stage6WalkingPatch.hpp` now has a **read-only** `visual_tile_offset` that composes two allowed +/-32000mm geodetic offsets for visual sampling only. It is strictly bounded to +/-64000mm, retains the existing no-synthetic-land / no-ocean / no-client-authority rules, and does **not** increase the native actor movement limit. Native geometry regression in `STAGE6F5B_ContinuousSurfaceMathTests.cpp` verifies valid source-only 64m offsets, rejected out-of-range values and unchanged direct native 32m limit. On locally compiled actual Linux game host, the original nine tile packet now publishes at all nine expected +/-64m positions, and `Stage06GInstalledSupervisor.py` passes **16+ ticks, one-writer contention, guardian loss, offline tick freeze, recovered generation and stale-session denial**. Native local SGDX five-test process custody suite also **5/5 PASS**, including 4,000 genuine worker launches.

These are source-compatible local tests, not final hosted Windows/Linux exact-source attestation. The next exact-head native and SGDX results MUST be reconciled at the replacement SHA, and physical Windows SCM/Job plus real delegated Linux cgroup/reparenting remain separate high-security gates.


## 2026-11-13 D7/E6/F/G original acceptance reconciliation (newer evidence)

- Original run [#43476579404](https://github.com/vjmichael/Single-galaxy/actions/runs/43476579404) at exact SHA `db3ebe89321c1336783583d95d50cc615478a15b`: **Linux original-source native batch passed all mandatory groups**, including D7 full 1,000kg two-voyage freight, E6 staffed ecology, F named-NPC movement and SQLite/1,024-tick four-cell supervisor endurance. The original v85 historical-producer/SQLite attestation succeeded. This is native source acceptance, **not installed gameplay/physical GPU acceptance**.
- The separate Linux SGDX-03 guardian/Godot bridge then failed because `tests/SGDX03GodotInteractiveTests.py` ignored the now-mandatory original `ONBOARD` read-only production frame. Two other SGDX checks passed; no worker-escape or SQLite custody failure was shown. The Windows job at `db3ebe8` was **cancelled**, so do not infer cross-platform completion.
- Repair [`ee07b42`](https://github.com/vjmichael/Single-galaxy/commit/ee07b42d03100389305d2cec402a1fa5ea4bb92d) parses exactly one `ONBOARD` frame for the correct request/session/ship/tick/generation, rejects duplicates, and checks read-only inventory/source flags. Nothing about the original kernel writer, release authorization or security policy was weakened.
- G acceptance script now requires not just the 1,024 original-world ticks, but the real **4 x 1,000 OS process launches**, native SGDX guardian/crash/restart, original SQLite restart and Godot/native pipe bridge in its **single machine-readable exact-SHA receipt**. A native `PASS` without those tests is forbidden. Worker-stress 300-second suite limit is aggregate only; per-worker deadlines remain strict.
- Remaining Stage 6 gates: successful matching **Windows and Linux** native + SGDX/SQLite original receipts at the **same final SHA**; Linux delegated cgroup containment and genuine physical-Windows SCM/Job evidence beyond hosted per-worker tests; production-scale cross-cell load/OS crash/recovery; separate installed/UI/physical Radeon when appropriate. E6/F feature source can advance without repeated physical GPU validation.
- No merge, release, unsigned NSIS installation or physical GPU acceptance authorized.



## 2026-11-13 evidence update (supersedes the earlier provisional native status)

The original run [#53](https://github.com/vjmichael/Single-galaxy/actions/runs/43455824013) on source `64f3c6a` compiled on Windows and Linux; each ran **38/39 native tests successfully**. The sole failure was D7's positive freight scenario before mining completed: original command ID 1, tick 1, `InvalidArgument`. It was a legacy 80-tonne `Mine` request against snapshot v126's mandatory one-tonne mining, not a missing receipt. The **current repair** is committed on the same Stage 6 branch at `b1272a2`, with finite fitted drill and onboard pilot and unique one-tonne requests. D7 new native outcome remains pending; independent receipt-contract CTest is mandatory in consolidated batch. Original historical v85 SQLite, E6, F, G and native worker custody gates passed at the prior SHA but **must reconcile at a single new SHA**. Installed-human and physical GPU gates remain separate; no repeated Radeon check blocks feature work.


**2026-11-12 | Development branch:** sg/stage6f5e-installed-terrain-recovery-20261112
**Status: source implemented provisionally; exact-head native, Windows/Linux, SGDX and installed Radeon acceptance NOT RUN. No merge/release authorized.**

## G1. Unified original-world admission

New header: include/galaxy/Stage6ConsolidatedAdmission.hpp.

The existing colony live host, desktop station host, desktop cockpit host, desktop planetary surface host and native shuttle transfer host now use one original-world admission rule. The former separate host migrations stopped at different schema versions (v130 ecology vs. v135 surface atmosphere).

The shared sequence uses existing PersistentWorld generation-fenced commits, in this exact order:
- v127: immutable physical body genesis and finite regional material;
- v128: actual local water/ice/soil/hazard environment;
- v129: original finite-source extraction custody;
- v130: original site ecology, farms, work, inventory and utility;
- v135: original sealed body gravity/atmosphere/survival foundation.

Original source reparse, tick, material kilograms, ore equivalents, credits, player people, sites and shuttles are checked before and after. The stages are individually durable/idempotent commits, **not one all-or-nothing five-stage transaction**. A failure must be reconciled from committed SQLite; no automatic original-save reset is allowed.

The F6 --planet-recover preflight remains **read-only**, without migration or world STEP. Only the subsequently admitted original writer can migrate. The audit preserves real uncrewed, damaged or stranded physical shuttles as material custody in otherwise valid historical saves; actual boarding/flight and on-shuttle player recovery remain independently denied without real pilot authorization.

## G2. Original saved-world audit and five-person-state matrix

The existing desktop player host now supports --stage6-audit: a read-only, original-save-only mode under both OS writer locks. GFRAME records original source tick, SQLite generation, canonical state hash, ore/kg/credits, finite-body/region/site/shuttle/person counts and version flags. It grants no gameplay action and never invents a world or advances simulation.

New native acceptance source: tests/Stage06GOriginalWorldAuditMatrix.py.

Tests compare genuine original saved orbit, boarded shuttle, descending shuttle, landed shuttle and player-on-surface checkpoints against independent native audit GFRAME, exact tick/generation/hash and matter. Concurrent audit must be denied while another native writer owns the SQLite world. Missing database must remain absent.

A concrete prior source-contract defect was repaired: original fixture --seed emits CHECK, but staged F6 readback expected TRANSFER. New explicit --seed-surface preserves the F5-E CHECK contract and provides genuine TRANSFER for F6 and S6-G.

## G3. Four authentic regional observations and 1,024 real durable ticks

New C++ test: tests/Stage06GConsolidatedMigrationTests.cpp, CTest name galaxy_stage06g_consolidated_migration.

An older real colony SQLite world undergoes injected snapshot-write rollback, original resource/hash/generation verification, v127-v135 upgrade, repeated idempotent admission, SQLite close/reopen and exact material parity. A genuinely scanner-equipped original colony ship with actual finite planetary genesis surveys four neighboring 36x72 original geographical/climate cells. All four are verified against the same body and coordinates without source mutation. The simulation then advances 1,024 original SQLite world STEPs in 16 batches with per-batch state hash, matter and generation verification, and reloads the saved world. S6G_SCALE prints observational elapsed milliseconds.

**Four neighboring surveyed regions are NOT four independent server workers.** Genuine multi-worker and cross-system 1,024+ tick load acceptance remains outstanding.

## G4. Exact-head batch acceptance tooling

New opt-in operator script: tests/Stage06GNativeAcceptanceBatch.py. It checks a clean source SHA, the actual native CMake source path, compiles the checkout, verifies named CTest registration, runs Stage 6 geography/finite-resource/physical/ecology/surface/person/recovery plus WO2–WO5 and DOCK07–09, and writes machine-readable result receipts with failure excerpts. Optional Godot source-only fixtures are included when installed.

A native matrix pass is explicitly *native only*, never installed acceptance or a release claim. **The driver and tests have NOT been executed or submitted to CI.** No source edits have been merged to main.

## Open acceptance and missing implementation

1. Exact new SHA: actual Windows MSVC and Linux native compile/CTest with original receipts, repair demonstrated failures, no inferred passes from test registration.
2. True historical SQLite v86 through v135, survey archive revision retention, WAL/failure injection, no loss/creation of resources, and genuine restart recovery. Existing WO3 historical import corpus issue remains open.
3. Real SGDX/OS supervisor and writer kill/restart/suspension, one OS writer, no orphan process, exact replay refusal and unresolved-action visibility, real Windows kernel and Linux acceptance.
4. Real Windows installed/Radeon journey: station/orbit -> manufactured shuttle -> physical planet arrival -> 3x3 original terrain -> suit/O2/survival -> hatch -> ascent/return; installed upgrade/rollback, source package/dependency integrity and sustained pacing.
5. Original independent multi-cell host scale and multi-body load; native four-region sample and 1,024 tick single-world test do not close this.
6. Earlier S6-D/E cross-region water/sediment transfer, soil/mineral/volatile freight, extended ecology and S6-F5-D original NPC surface movement remain separate functional gaps.

**Exit:** S6-G source is staged provisionally; Stage 6 consolidated acceptance and release gates remain OPEN. Next engineering work is the Stage 6 acceptance/failure-repair pass and the outstanding D/E/F integration gaps, then Stage 7 population/society.
