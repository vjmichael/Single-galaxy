# S6-G — Consolidated original Stage 6 integration

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
