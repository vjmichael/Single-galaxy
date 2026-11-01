# ECON-02B2d18d — Shared NPC duty boundary and visual acceptance strategy

**Date:** 2026-11-01
**Project:** Original Single Galaxy prototype, independent from EVEmu
**Baseline:** `SingleGalaxy_ECON02B2d18c_Color_And_Spatial_Gates_2026-11-01_Source_Tests.zip` (636 entries)

## What changed

`Simulation::generic_operator_assigned(site,npc)` provides a read-only query of the authoritative, persisted generic-machine staff assignments. `NpcConstructionCrews::validate_worker()` rejects a resident who already operates a generic machine. This applies to construction, hauling, evacuation, dismantling and retirement uses of the existing worker validator.

The single-threaded colony control host now checks `Crews::worker_has_duty()` before accepting a nonzero `GENERIC_OPERATOR` assignment. This method examines assigned construction workers, active restock/salvage jobs, dismantling, workshop evacuation and retirement assignments, as well as physical construction cargo owned by the worker. A paused crew assignment is still reserved, not free capacity. An authorized `GENERIC_OPERATOR ... npc=0` release is unaffected.

No simulated resources or inventory receipts have been altered. This closes a **cross-subsystem double-staffing gap**, but does not turn the existing factory into a fully physical NPC-delivered factory.

## Verified tests in this work session

- Full native simulation library, colony host and existing d18b site factory test targets compiled on Linux with CMake/Ninja.
- `ECON02B2d18d_SharedStaffing_HTTP`: real localhost relay + SQLite. Assignment to generic machinery blocks construction; releasing operator enables crew assignment; crew assignment blocks new machine operation; the conflict survives process restart; forged session rejected.
- Earlier `ECON02B2d18b_GenericFactory_HTTP`, `ECON02B2d18b_AuthoritativeFactorySite` and `ECON02B2d18c_SpatialHost_HTTP` passed.
- `node --test tests/*.js`: 93/93 passing.
- Offline Chromium UI smoke: structure panel, generic factory controls and crew controls all passed (3 scripts). These browser tests validate interaction and layout, not full 3D gameplay or live browser-to-host acceptance.

**Not verified here:** the full 114-test native CTest suite, native Windows build, Windows interactive GUI, NPC-supplied stock transfer to arbitrary generic-machine ports, or unified future job scheduler across every colony occupation.

## Current visual reality (not a rendering-quality claim)

The source's `viewer/interactive.html` contains a top-down HTML Canvas space viewer and a large engineering/colony control panel. The factory designer has a grid and clear interaction states. These are debug/prototype views; the source snapshot does not yet demonstrate a modern 3D flight and settlement presentation or enough original production assets to judge final art quality. A passing simulation test does not prove the game is fun, responsive, visually coherent, or accessible.

### Proposed immediate visual/playability gate before additional large systems

1. One original 3D colony ship with a readable silhouette, a clear forward direction, thruster cues, and free flight through a small playable region.
2. A simple asteroid with scale, light/shadow, and a visible mining/salvage feedback loop, derived from real authoritative mining events.
3. One buildable white colony structure plus selected player color accent; one actual construction animation and completion state, with existing server authority preserved.
4. User experiences launch → undock → fly → mine → carry → return → dock/unload → save/relaunch. Record whether controls are understandable and enjoyable, independently of backend unit tests.
5. Visual acceptance: comfortable camera, legible HUD at typical desktop resolutions, distinct silhouettes, stable frame pacing, coherent lighting and materials, predictable interaction feedback, and no misleading indications of physical resources.

This is an **acceptance proposal**, not a claim these visuals exist or have been implemented. Avoid buying or generating a full library of art assets before the vertical slice proves that the original game interaction loop is worth expanding.

## Remaining engineering gates

- d18c: physical NPC deliveries to generic machines through built transport systems; remove remaining instantaneous warehouse↔machine transfers where physical custody is promised.
- d18c: coordinate named jobs with aggregate colony staffing and production scheduling, without double allocation.
- Resource-center service regions, utility layers and power/water/atmosphere are approved design directions, but not present in this patch.
- Windows GUI native acceptance and interactive visual vertical-slice gate remain open.

GitHub is the repository/documentation record, not the development hub. This patch was produced locally and has not been uploaded.