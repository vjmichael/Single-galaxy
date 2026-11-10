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

## FP3D-UX-02 — creator's Level 1 boarding blocker (2026-11-09)

The creator's actual Windows **Radeon AI Pro R9700** game launched but
boarding was still difficult. Two demonstrated defects explain it:

1. **Cyan hatch tile occluded by the floor.** The native (0,0) threshold
   is covered by a `Hangar gangway` box whose top is at Y=0.030 m.
   The old 15 mm hatch mesh at Y=0.020 m had a top at **0.0275 m**,
   i.e. **2.5 mm below** the gangway. The tile appeared missing even
   though the C++ 600mm square boarding rule was correct. The visual
   marker is raised to Y=0.0775m (top 0.085m); a cyan floating
   `Label3D` sign above the same coordinate says `BOARDING HATCH`.
   These are **noncolliding presentation only** and do not teleport
   the registered native PERSON or change the original hatch location.
2. **E was silently discarded during a native WALK acknowledgement.**
   The station bridge had `if ... _pending != 0: return` and would
   drop E if a player pressed it while moving or immediately after
   releasing a key. E was additionally gated on FPS mouse capture,
   even if the actual standalone game window had focus. The bridge
   now queues **at most one explicit E** only when the outstanding
   command is WALK. After a fresh PERSON/SQLite-generation
   acknowledgement it dispatches the native BOARD request (and the
   original C++ server repeats owner, docked-ship and exact distance
   verification). Rejection, crash, role change, timeout or shutdown
   discards the pending E. E now works with the mouse released while
   the window retains keyboard focus; native walking/thrust still
   require focused, captured input.
   Outside the exact ±600mm square the client explains how to reach
   the hatch instead of silently ignoring E. **No client-side
   interaction grants ownership or custody.**

The true station-person genesis in original
`PLAY_STATION_02_BoardingTests.cpp` is **(1600, 0) mm**. From the default
forward-facing camera, pressing **A** toward the left for approximately
1–2 seconds reaches the nearby native (0,0) floor hatch. Release A,
then E. P after original BOARD moves to the protected cockpit, E
requests the pilot seat and U undocks the original vessel.

**Visual specification applied:** station geometry medium gray,
original owned ship exterior/interior white, raised cyan boarding
tile/sign; debug status in a compact top-left system panel; flight
SHIP/ROCK telemetry separately at upper right only when a real
original SHIP frame exists; FPS bottom-left; contextual action
bottom-center. This avoids the observed top-left text collisions.
No full materials/assets pipeline or manufacturing claims.

**Tests:** `interaction_prompt_smoke.gd` now checks that the
real Godot floor marker clears the gangway, sign is present,
station/ship material colors differ, and the HUD panels do not
overlap; `boarding_action_pipe_smoke.gd` tests the Godot bridge
with a synthetic pipe and the accepted PERSON/BOARD protocol:
a pending WALK+E produces exactly one request using the fresh
SQLite generation, no request outside the 600mm C++ hatch, no
replay on native DENY and no command while disconnected.
These are in the grouped Linux/Windows real Godot tests and
the extracted Windows alpha package smoke suite. They do
not replace the independent native C++/SQLite boarding tests.

**Acceptance still outstanding:** rebuild/test the complete
new task-group source with ONE consolidated native Windows/Linux
gate; publish an updated exact-source ZIP only after it passes;
then the creator must actually board, sit, undock, collide, dock
and restart on the physical R9700. PR #125 remains DRAFT and
manufactured mining equipment remains deferred.

### Post-feedback boarding guidance refinement

The lower interaction panel now derives a **single next WASD direction**
from the current original `PERSON(x_mm,z_mm)` and the actual Godot
player/camera yaw using `Basis.inverse()`; the display updates as the
player turns. At the true native genesis x=1600mm/z=0 with default facing,
it says **Hold A toward hatch**. After turning +90° it correctly says
**Hold W**, and guidance disappears at the genuine ±600mm boarding bound.
This is purely read-only routing; it never injects keys or alters custody.

The top-left status panel is now compact, status labels auto-wrap, FPS is
fixed bottom-left and flight SHIP/ROCK telemetry appears separately at
upper right only after verified original flight observations. This also
implements the creator's requested **gray station / white ship** visual
separation.

A real Windows/Linux Godot smoke verifies both directions and the
threshold; the file-backed synthetic pipe regression verifies one
explicit E buffered through an in-flight native WALK and a subsequent
native BOARD command using the newly acknowledged generation. The C++
host remains the only boarding authority. After source changes, an
updated exact-head consolidated Windows/Linux/Godot/ZIP gate remains
mandatory before giving the creator a replacement package.

## FP3D-UX-03 — no manual H or P in normal player flow (creator Radeon R9700)

The creator's latest real Windows screenshot confirms a better HUD and
visible cyan boarding sign, but the panel says **STATION NATIVE DISCONNECTED:
Native station host exited; saved player location retained**. Pressing
H gives no useful result. These symptoms are a **real blocker**: the
standalone Godot scene is visible but the supervised C++ host is not
running. A rendered gray station alone cannot authorize boarding.

**Correct product behavior:** launch the saved original station and
register a valid C++ PERSON automatically on player window start.
H was a temporary manual development test switch; it should not be
part of first-person gameplay and must no longer toggle a healthy
station/ship host off. A genuine E BOARD receipt automatically requests
the protected station→cockpit SQLite writer handoff, without P. E in the
cockpit requests the native seat; U undocks after a real docked SHIP
receipt. On reopen, a saved PERSON already aboard is routed into the
cockpit automatically through the same protected handoff.

**Failure behavior:** do NOT silently create/recreate a save, grant
local Godot movement as if the station were still authoritative, or
start an unsupervised writer. Native startup is attempted once. On a
failed or prematurely exited C++/SGDX child, the UI presents a real
connection failure, a **Retry connection to saved station** button,
and H as an optional recovery shortcut only. The client captures up
to 4 KiB of child stderr AFTER process exit, retains one bounded
`sg_station_last_failure.json` in private Godot user data, and if the
creator launched via the standard packaged playtest CMD also writes
that bounded diagnostic to `desktop_godot/playtest-logs/` for inclusion
in `SingleGalaxy_Station_Playtest_Evidence.zip`. No private SQLite,
dumps or screen recordings are included in that diagnostic. The exact
root cause of the creator's existing world exit is **not established**
without its native stderr; no claim of repairing that specific cause
is permitted until actual physical evidence arrives.

**Testing:** use genuine Godot 4.7.2 + SGDX + original C++/SQLite in
one consolidated native Windows/Linux gate. Dedicated CI-only sandbox
test `auto_station_connect_acceptance.gd` refuses any preexisting
world; explicitly invokes the production auto-start entrypoint,
verifies original PERSON at x=1600mm, 12 admitted 90mm WALK updates,
E BOARD with original custody, automatic station→cockpit handoff, then
reopens the SAME saved player already aboard without H/P or a second
writer. Run only under `SG_FP3D_CI_AUTOSTART=1` and fresh dedicated
XDG_DATA_HOME/APPDATA. This test is NOT run in the user's extracted
ZIP preflight, which must never mutate a player's world merely
to verify the package. Real physical Radeon AI Pro R9700 confirmation
remains the final acceptance gate; PR #125 remains draft and
manufactured mining equipment remains deferred.
