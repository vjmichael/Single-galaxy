# DEMOG-05q6e5b8i — Consolidated Single-Galaxy game kernel, v60 local staging

**Date:** 2026-11-03. **Project:** Original Single-Galaxy C++17/SQLite, not EVEmu.
**Disposition:** **local merged source + native acceptance passed**; **GitHub source merge, installed native Windows game and live multiplayer ingress remain open**.

## What was implemented and verified

The actual original game's `Simulation.cpp`, `Persistence.cpp`, public headers, client contract, colony host executables, factory construction code and game tests were merged with the independently verified v59 shipment/production code in a **single** `galaxy_kernel` static target. Both source baselines were recovered from frozen project archives. The original main-branch source SHA values for the core files matched the archived source baseline. The additional regional host/custody/interest/persistent-world modules also compile into this single target; `galaxy_regional` is an **INTERFACE compatibility target**, not a competing regional simulation implementation.

**No second authoritative world, cargo ledger, PersistentWorld, or RegionalModeAuthority is compiled.** The existing player-facing game executables link to the unified kernel. Existing inter-system physical shipping, factory receipts, three- and four-owner consignment, diversion/return, settlement, provenance, and source-region cold/detailed shipment handoffs retain their old canonical SQLite owner and resource-ledger rules.

A new end-to-end compatibility assertion uses the *original game* `ServerViewBridge` and `ClientView` after real four-owner freight settlement, checking a current-world snapshot and the next committed delta through the same authoritative Simulation. System-local visibility is preserved; manifests are **not** yet displayed as a GUI feature.

## Frozen old-save compatibility

The earlier game kernel generated a **genuine v20 snapshot** with six original NPCs. The new kernel parsed it byte-exactly, advanced world ticks, persisted it through SQLite and performed independent recovered-world reopens with the same NPC count and crew. Ten older test assertions had demanded v20–v30 headers; the merged workforce initialization correctly emits v31 to encode richer passenger records. Only these literal format assertions were updated in the old tests; their functional and recovery assertions remain unchanged.

## Executed results (local C++ toolchain)

| Verification | Result |
|---|---|
| Full merged Debug CMake/Ninja build of existing game and v59 tests | **PASS** |
| Full combined native registered CTest | **138/138 PASS** |
| Existing regional regression targets ECON03c2–c8b | **8/8 PASS** |
| Merged UndefinedBehaviorSanitizer, freight + regional + old snapshot | **3/3 PASS** |
| Guarded source-overlay dry run on independent regional baseline | **PASS** |
| Overlay applied to independent regional source tree; fresh build/focused CTest | **3/3 PASS** |
| Final clean ZIP extraction; separate GCC build/focused CTest | **3/3 PASS** |
| Archived source file SHA-256 integrity | **682/682 PASS** |
| GitHub main source merged / native Windows installed app / production multiplayer identity | **NOT DONE** |

The archive includes build/CTest/UBSan logs and `tools/apply_v59_canonical_merge.py`. The script defaults to read-only preflight and checks frozen Git blob baselines before writing. An explicit `--apply` overlays the main source and rewrites CMake in place to remove the duplicate regional static library while preserving other main targets. A checkout is required. It does not push any commit or mutate GitHub by itself.

## Reproducible deliverable

- `SingleGalaxy_UnifiedGameKernel_v60_2026-11-03.zip`, **1,851,470 bytes**.
- SHA-256: `175ca08084d2b4680608c6ec90b56c0ea9c1a8ff8f08bb2bacf013f10dd04f96`.
- Source archive: unified game code, 138 registered test definitions, legacy binary-compatible test fixture, main-branch merge tool and expected blob manifest, verification evidence and 682-entry `SHA256SUMS.txt`.

**Important:** this new GitHub engineering report is documentation only. **Do not claim the source is committed to main.** The local source ZIP is the working merged code; to complete deployment, apply/review it on a fresh GitHub checkout, rerun native regression and commit a feature branch/PR. This source update was not uploaded to the GitHub repo through this call.

## Remaining work

Trusted live-client session identity, authenticated freight command ingress in the actual colony GUI/host, human-visible manifest UI, multi-process regional fencing under concurrent load, upgraded constructed equipment/staff/utility integration beyond the 2-machine fixture, native Windows release and installed-game acceptance, and branch source publication. No production-level multiplayer safety claim.

**Safety:** dependent fatalities remain blocked; no child-harm depictions; police/fire/institutional EMS deferred.
