# ECON-03c7 — Durable regional fidelity ownership (2026-11-01)

**Status:** Linux locally verified, opt-in isolated regional module, **not** integrated with the playable `PersistentWorld`. GitHub is a repository/archive, not the development or CI environment.

## Implemented
- New `RegionalModeAuthority` records one per-region owner (cold or detailed), transition phase, unique handoff identity, monotonic generation, checkpoint revision and boundary tick in SQLite.
- `prepare` writes a durable intent after checkpoint/tick CAS; all cold scheduler commands are blocked while pending.
- `commit` requires a trusted same-transaction SQLite staging callback for detailed snapshot promotion or detailed-effects reconciliation on demotion. Callback/SQLite faults roll back staging, checkpoint update and ownership flip together. A test callback is **not** proof of actual NPC/ship/material receipt validity.
- `abort` revokes an incomplete handoff and increments owner generation. Restart never silently resets ownership.
- The existing cold host command path now requires the active owner generation once enrolled. Stale workers are rejected even if holding a valid checkpoint revision. Legacy unreceipted checkpoint writes are blocked after enrollment.
- Each committed cold regional command keeps the authority revision and tick in sync atomically with the checkpoint and command receipt.

## Local verification
- Release CTest: **6/6 pass** (ECON-03c2 through ECON-03c7).
- ASan/UBSan Debug CTest: **6/6 pass**, `ASAN_OPTIONS=detect_leaks=0`.
- Source patch applied cleanly to exact isolated ECON-03c6 baseline; rebuilt patched copy **6/6 pass**.
- Tests: handoff restart, rollback of staged receiver data on injected SQLite fault, stale generation rejection, pending transition fencing, demotion, durable abort, region isolation.

## Recoverable local artifacts
- Source ZIP: `SingleGalaxy_ECON03c7_Durable_Fidelity_Handoff_Isolated_Source_Tests_2026-11-01.zip`, SHA-256 `f959b58e9a71de5cd4d4fe78079cc30dd6ac763987a070276a2b5d2892d9f1f3`
- Patch: `SingleGalaxy_ECON03c7_Durable_Fidelity_Handoff_2026-11-01_Changes.diff`, SHA-256 `e86dfd998017366139a128a928b72c2d96a46e376149dfe44041ed8b71af61ac`
- Engineering report: `ECON03c7_Durable_Regional_Mode_Ownership_Engineering_Report_2026-11-01.md`

The full artifact bytes are separately delivered in the conversation; this GitHub document records the milestone and **does not claim the source ZIP or patch were pushed to the repository**.

## Outstanding
Actual `PersistentWorld`/host inventory and worker adapter, independent NPC/carrier receipt validation, durable active-object snapshots, real `RegionalInterestManager` hookup, multiplayer worker containment, full-game acceptance, Windows GUI and renderer/impostor implementation. The staging callback is host-trusted but not independently authenticated by this isolated prototype.

The separate graphics direction is stable-silhouette meshes and multiview 2D impostors with screen-space fading; avoid obvious polygon reduction popping. Graphics LOD and authoritative simulation ownership remain separate.
