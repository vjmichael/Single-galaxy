# FP3D-01 — First-person transition, no artificial game authority

Status: native-authoritative, read-only 3D *camera slice* implemented and unit-tested; full FPS on-foot movement **not yet implemented**.
Source baseline: Single-Galaxy draft PR #100 (SPACE-05M R5), stacked on #98, #97, #95.

## Product decision

The original one-galaxy, finite physical inventory, persistent NPC, mining/hauling, construction and original combat simulation is not replaced. Its *presentation and interaction model* will transition to a first-person game, starting with untextured 3D primitive geometry. Hands, arms, full-body animations, Materials/texture authoring, and detailed models are explicitly out of scope at this stage.

The user wants a real first-person shooter game, not merely a different camera on a resource-management UI. The first slice is deliberately only an optional read-only cockpit visualization; it does **not** claim to deliver on-foot FPS gameplay.

## Control contract

| Mode | WASD | Mouse | Up/down arrows | B | Escape |
|---|---|---|---|---|---|
| On foot (future FP3D-02) | Walk/strafe | Look with pointer lock | Not ship propulsion | No ship brake | Release mouse |
| Piloting ship (existing / future cockpit) | Held steering, as the user's chosen 2D-to-3D migration mapping | View/look, no steering until authoritative pilot look exists | Thrust / reverse | Native counter-thrust to stop | Release mouse |
| Tactical map | No new world-command authority | Select verified scanned targets | Existing ship commands only when piloting | Native brake, when available | Normal UI |

**No conflation of standing/walking and piloting input.** The engine must own seat/board/unboard transitions and authority. Key release, window focus loss and disconnection stop submitting new input. Existing velocity persists in vacuum until physically countered.

## FP3D-01 delivered slice

- WebGL2 renderer, no runtime CDN or third-party rendering engine. Untextured, low-poly primitive proxy geometry: ship, asteroid, station, gate, wreck.
- Native SGVIEW v1 remains authoritative: only frames accepted by the existing hash/sequence validator reach the renderer. The renderer cannot send a command, grant ship ownership, edit cargo, simulate collision or advance state.
- First-person camera anchored to an **owned (owner 7)** ship, using the native ship yaw bound to that exact ship ID. No foreign-ship fallback, no guessed heading, and no cross-system rendering.
- Existing SGVIEW feeds only two physical position axes. Display those as an X/Z horizontal plane with an illustrative Y for primitive geometry. **This does not add physical vertical movement** or create imaginary Z positions in the save file.
- At most 2 km of nearby contacts drawn as lightweight object primitives. Sizes are visual proxies, not collision geometry or canonical measured dimensions.
- Optional cockpit toggle in the existing interactive viewer. Default remains the established 2D tactical map; WebGL2 unavailable -> retain the map and explain failure.
- Crosshair is noninteractive artwork. No player model, hands, arms, weapon fire, recoil or rig is inferred from a 2D frame.

## Next protected implementation gates

1. **FP3D-02 native on-foot character:** physical player-person authority, world position + true Z, capsule collision, ground/contact model, bounded walk/strafe/mouse-look commands, SQLite and journal recovery; no client-side teleport or second position writer.
2. **FP3D-03 board/unboard and cockpit:** player changes seat/vehicle only after native admission; merge WASD walking vs ship steering mapping without an identity race, maintain flight inertial physics; first-person camera and independent third-person/tactical debug view.
3. **FP3D-04 FPS interaction:** range/line-of-sight raycast for doors, objects, NPC conversations, mining, inventory, salvage and eventually original combat. Native game decides all results. Weapon visuals can remain primitive beams/projectiles and a center reticle; no arms required.
4. **FP3D-05 visuals and acceptance:** 3D pose and environment source versioning, opaque geometry, clipping, shadows/lighting later, high-order floating origin, crossing star/cell boundaries, asset-less first install, Windows real device input, frame pacing/large world; Linux regression and crash/restart.

## Tests and acceptance

- Source-level FP3D-01 test ensures native owner scoping, same-ship yaw, non-default star system, relative millimeter-to-meter conversion, foreign cargo privacy, no external command calls, and retention of the original 2D path.
- Targeted native viewer/control tests run under Node without a GPU.
- A WebGL2/Chromium smoke test on this Linux build host was **blocked by EGL / xcb display initialization**; this is not a rendered-pass assertion.
- **Windows browser WebGL2 real rendering and FPS controls are not yet accepted**; full 3D native walking has not begun. This viewer slice must stay behind an optional preview toggle.

## Relationship to open recovery work

The owner-id/generation-bound native cyber operator journal-replay receipt is still a separate in-progress recovery task. This first-person preview changes no C++ persistence, no command replay and no game simulation code; integration should not merge unfinished native recovery work accidentally.
