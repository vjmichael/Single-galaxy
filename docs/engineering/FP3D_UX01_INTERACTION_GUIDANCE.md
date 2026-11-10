# FP3D-UX-01 — Native interaction prompts for the actual Windows playtest

## Creator-observed problem

The first creator-run AMD Radeon Windows playtest (2026-11-09) successfully
opened the packaged Godot 4.7.2 + supervised original C++ game and displayed
the station's monochrome 3D scene. However, the ship looked like an anonymous
white obstacle, the boarding location was not identifiable, text overlapped
at the upper left, and F8 did not exit the standalone game.

The screenshot **did not** show successful native boarding or flight:
`LOCAL MOVEMENT TEST ONLY`, `SHIP: native host disconnected` and stale
`FLIGHT SENSOR` mean the native cockpit session had not yet been engaged.
No physical GPU collision/docking/restart acceptance is inferred from the
window opening. PR #125 remains **draft**.

## Correct owner procedure for this build

1. Click **inside the game window** so it owns keyboard and mouse input.
2. Press **H** once. Wait until the actual original station PERSON receipt
   shows `NATIVE STATION PLAYER` (not merely a local preview).
3. Follow the **cyan floor tile near the hangar gangway** at C++ station
   coordinates (0 m, 0 m); do not press E at the white visual ship hull
   eleven metres farther along the corridor.
4. When the native PERSON position is **within ±600 mm on both axes** and
   the same receipt identifies an eligible **owned, docked** vessel, the
   new bottom-center panel says **Press E — Board owned ship**. Press E.
   C++ `Simulation::station_player_action(Board)` separately verifies
   exact distance, ship custody and docking. The UI does not authorize
   a transition itself.
5. After the native BOARD receipt, the prompt says **P — Enter cockpit**.
   Press P and wait for an admitted original ship POSE. Approach the
   cockpit console and press **E** to request the seat; native authority
   verifies seating. Only after a seated/docked ship receipt will the
   prompt say **U — Undock**. The remaining pilot keys are V exterior,
   arrow keys thrust, A/D yaw, B brake and G docking.
6. Press **Esc** to release the mouse. The standalone game quits via
   the window **X** or **Alt+F4**; F8 only stops play in the Godot editor.
   F11 captures the visible game viewport with source receipts when the
   packaged launcher's evidence directory is configured.

## UX/source constraints

- The bottom-center **InteractionPrompt** panel is high contrast and
  intentionally read-only with `MOUSE_FILTER_IGNORE`. It does not
  intercept E or issue native commands.
- `interaction_prompts.gd` computes availability only from the latest
  admitted native `PERSON` plus an active supervised station session,
  or an independently admitted ship `POSE`. Before that, it asks for H
  or says it is connecting. It never uses collision with a white mesh,
  unsupervised local walking, a cached former player location or a
  stale original ship frame to assert that boarding is possible.
- Boarding uses the **exact original C++ axis-aligned square** bound
  (`abs(x_mm) <= 600 && abs(z_mm) <= 600`); the refinery hint uses
  the native terminal's 1,200 mm circle at (3 m, 0 m).
- The hatch is a cyan, noncolliding `MeshInstance3D` at native (0,0).
  All original C++ ship/asteroid collision, station ownership, physical
  persistence, protected Job Object custody and SQLite writer fencing
  remain unchanged.
- The action label reads the actual `InputMap` physical binding instead
  of baking in a fixed E; controller remapping still belongs to the
  separately deferred FP3D-INPUT task.
- Long native telemetry and error labels now wrap rather than painting
  over each other. The bottom prompt is kept away from the central reticle.

## Verification and next gates

`interaction_prompt_smoke.gd` runs inside the **real Godot engine** on
Windows and Linux, confirming the visible marker/panel, actual InputMap
key, exact 600 mm boundary, no eligible-ship denial, refinery hint, handoff,
cockpit and undock states. Its fixtures are **synthetic UI test inputs**;
they do not constitute native authorization. The original C++ boarding,
denial and SQLite restart cases remain independently native-tested. The
same real Godot smoke is included in the extracted Windows ZIP's bounded
positive launch tests, and source contracts assert that no hint becomes
a second writer.

This UX source change **invalidates the previous exact-head acceptance
until the next single end-of-task-group Windows/Linux gate passes**.
Afterward, deliver the new ZIP for the creator to confirm boarding,
cockpit, physical undocking, actual asteroid collision, docking and saved
world continuity on the physical Radeon. Manufactured ship mining equipment
remains deferred until that 3D physical acceptance.
