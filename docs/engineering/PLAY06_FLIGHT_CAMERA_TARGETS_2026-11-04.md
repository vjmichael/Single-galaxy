# PLAY-06 — Camera, Target Selection, and Flight Usability

**Single Galaxy / 2026-11-04**

## What was built
PLAY-06 makes the existing ship-first top-down player experience easier to operate. The camera is view-only: default follow centers on the player's simulated colony ship with a practical 30,000 mm viewport; Overview frames native-visible entities, and wheel / ± buttons zoom within the bounded 0.35×–8× range. Reset View returns to following the ship. The camera uses screen-coordinate transforms consistent with drawing, so a canvas click can target a visible asteroid by deterministic nearest hit-testing.

Asteroid selection is **not authorization**. Clicking a visible rock only changes the dropdown and highlight if its ID was actually received from native `SMALL_ROCK` or `ROCK` scan telemetry in this session. The separate native `colonyMineTarget` command continues to validate the target, distance, ore, inventory capacity, and identity.

Fixes the visual heading issue from PLAY-04 where the rendered ship icon would not rotate while stopped even when native yaw was changing. Replay hotkeys remain isolated from live piloting so arrow keys and Space do not also navigate recorded frames. All existing Q/E yaw and WASD/arrow thrust remain intentionally world-axis based as in PLAY-04. The optional tutorial stays skippable.

## Acceptance
- `node --test tests/FlightCameraTests.js tests/Play06WiringTests.js`: camera math, zoom bounds, ship follow and world-to-screen targeting, native scanned-ID gate and real browser/relay static inclusion.
- Existing PLAY-05 feedback/wiring, PLAY-04 cargo, PLAY-03 piloting, and PLAY-02 tutorial tests are also run in focused CI.
- The local relay's static allowlist is updated to serve `flight-camera.js`.

**Limits:** This is still a 2D top-down client, not a completed cinematic 3D flight cockpit. It does not add a physical player avatar, autopilot, starship torque simulation, NPC migration, or a new resource authority. Native-host-to-browser live play, scrolling/picking on Windows, and real frame-rate/user-feedback acceptance remain to be verified separately.

Player embodiment remains a design decision: direct ship-control opening is the implemented behavior, not an approved walking-avatar / boarding mechanic.
