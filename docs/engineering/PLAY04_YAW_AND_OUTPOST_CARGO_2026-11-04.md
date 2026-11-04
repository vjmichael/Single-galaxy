# PLAY-04 — Persistent Yaw and Real Outpost Cargo Return

**Date:** 2026-11-04. **Branch:** `integration/play04-yaw-cargo-20261104`.

## Progression

1. Start with a real colony ship and finite crew; find, mine, refine, fabricate and construct the first player-owned outpost (PLAY-01/02/03).
2. Fly to an asteroid and mine bulk ore into the ship's finite hold.
3. Pilot back toward the constructed outpost using displayed relative X/Y and distance. A brake control applies **one** physical counter-thrust impulse per click; it does not teleport, stop instantly, or override native velocity limits.
4. Once within the existing **2,500 mm** docking range and stationary, use **Dock at home outpost**. Native `RequestDock` enforces transition, range, motion, system and permission.
5. Unload or reload **bulk ore** using existing `UnloadOre` / `LoadOre`, exchanging it atomically with the actual player-specific hangar at the outpost. Undock using the existing transition.
6. A reopened SQLite world must retain the ship heading, cargo, hangar stock and outpost inventory without duplication.

### Yaw semantics

`Q` = left yaw (−15°), `E` = right yaw (+15°). On-screen buttons provide the same actions. The existing authenticated relay accepts exact signed yaw increments, not arbitrary angle changes. A native `Thrust` command with **zero X/Y** and amount ±15 changes the authoritative ship `heading_deg`, wraps to 0–359°, and upgrades the persisted entity snapshot to **v61**. Existing thrust controls still apply **world-axis X/Y translation**, not heading-relative acceleration; yaw is not a cosmetic browser rotation. A distinct rotational dynamics model, torque and full 3D flight can be handled in a future piloting iteration rather than silently changing existing control physics now.

The host emits strictly decoded `HEADING` telemetry and the client displays it. Any native command requires the original authenticated player identity. The ship must be in space and not actively jumping. The ship's angle survives save/reload and recovery-copy.

### Cargo and home-base authority

The local relay only exposes `colonyDockBase`, `colonyUnloadBase`, `colonyLoadBase` and `colonyUndockBase` to authenticated colony mode. The native host derives a dock target from **real owner-matched `built_outposts()` entities**, not from a browser-supplied ship, station, owner or inventory ID. Native authority still checks docking, motion, hangar capacity and ore quantity. For unload/reload, the host checks that the ship is docked at an owned, constructed outpost; no arbitrary station credit or material creation.

`viewer/cargo-return.js` is read-only. It derives relative navigation and button readiness from the live world projection and a whitelist of outpost IDs emitted by native `OUTPOST` telemetry. UI readiness is advisory; it does not replace native validation. The player can skip or show the earlier tutorial at any time.

**Limit:** `UnloadOre` and `LoadOre` move *generic/bulk ore only*. Geological mineral stock, refined material, modules, finished goods, manufactured components and life support inventory are not silently treated as bulk ore. Those resources retain their existing native manufacturing, supply and custody workflows.

## Verification contract

- `tests/PLAY04_YawCargoReturnTests.cpp`: owner authorization, ±15° yaw/wrap, invalid turn rejection, snapshot persistence, first outpost, dock, finite bulk unloading/reloading, conservation, recovery-copy and undock.
- `tests/FlightControlsTests.js`: keyboard yaw, focus and modifier safety.
- `tests/CargoReturnTests.js`: range/velocity readout, dock and hangar readiness, foreign station refusal and verified-outpost-only navigation.
- `.github/workflows/play04.yml`: focused CMake/CTest native run, build of actual colony host, Node browser-contract tests and script/relay syntax checks.

**Not yet verified by this submission:** human-operated Windows/Chromium end-to-end playthrough, live HTTP→native command→SSE round-trip for the new actions, long-duration piloting feel, automatic route following, a final visual ship yaw rendering, or complex material stack transfers. Do not mark those accepted merely because code is committed.

**Scope freeze:** avoid returning to the NPC ferry/migration subsystem while making the core mining and outpost loop playable.
