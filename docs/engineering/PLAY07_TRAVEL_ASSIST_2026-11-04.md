# PLAY-07 — Optional Travel Assist and Manual Flight Override

**November 4, 2026 — branch `integration/play07-travel-assist-20261104`**

## Functionality
The existing colony ship and native simulation remain authoritative. PLAY-07 adds *optional* route assistance for two already-known destinations:
- a real currently visible asteroid whose ID was revealed by native `ROCK` or `SMALL_ROCK` scanning and selected by the player, or
- a real player-owned and constructed home outpost revealed by native `OUTPOST` telemetry.

The player engages Assist to selected asteroid or Assist home. The browser reads committed world projection (X/Y, velocity, system, dock/jump state) and sends **one existing authenticated `thrust` command at most per new native tick**, withholding further commands while the HTTP request is outstanding. The bounded planner uses the native acceleration magnitude of **250 mm per tick²**, explicit counter-thrust and a stopping-distance approximation. It does not set or overwrite ship position, velocity, resources or orientation directly; all effects are decided by the C++ engine and persisted by the native world.

Arrival requires position within a 500 mm target radius **and zero velocity**. It only disengages the assist and reports navigation complete; it never performs mining, constructs an outpost, docks, or transfers items automatically. Q/E yaw and world-axis WASD manual control retain PLAY-04 semantics. No player avatar/boarding design is introduced.

## Safety of control handoff
Manual steering/yaw or any other manual native command cancels the assist. The user can also click Cancel assist at any time. It does **not** magically brake or erase momentum; the ship retains native velocity. The assist auto-cancels when a target disappears, is no longer in the same system, ship changes, ship docks, jumps, a live stream disconnects, a session reconnects/signs out, native HTTP command is refused, an authoritative native RESULT reports rejection while assist is active, or the 1,800-tick bounded window elapses. It does not run in the background without a live authenticated browser session.

## Scope and caveats
The algorithm is intentionally modest, providing a first-pass manual-experience improvement rather than claiming optimal control, cinematic warp travel, long-range pathfinding or obstacle avoidance. It relies on live browser/network responsiveness and may require further tuning around a target, especially with server latency. Native physics is unchanged. Route selection is restricted to native-observed entities; a browser target is not an authorization.

## Verification
- `tests/TravelAssistTests.js`: braking, arrival, native-command bounds, jump/dock disallow, owner and visible target checks.
- `tests/Play07WiringTests.js`: HTML/relay script allowlisting, one-command-per-new-tick guard, cancel on manual command, session-loss behavior, regression for inherited malformed JavaScript.
- `.github/workflows/play07.yml`: Node tests from PLAY-02 through PLAY-07, JS syntax and Python relay parsing.

**Real live Chromium/Windows play acceptance remains open** until tested in the installed browser against the authoritative colony C++ host. The previous PLAY-04 native save/reload and ore conservation acceptance must remain intact.

## Additional fix
Recovered a malformed `let window.GalaxyResetExpedition?.();` line inherited from PLAY-05/06 that could prevent the live client from parsing. This is a functional blocker for any gameplay UI.
