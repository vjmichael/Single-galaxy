# FP3D-PILOT-01 — Authoritative ship role: pilot only, permanent third-person camera

**Creator gameplay rule (2026-11-09):** When a player boards a ship, they are its **pilot**, not an on-foot passenger in a walkable cabin. The ship view is **always third-person**, including when docked, departing, in space, and returning. The player **cannot stand up, wander the cabin, or switch into first-person while aboard**. The only ordinary player exit is a **single E to disembark after a genuine original C++ dock-back at the colony**.

## Hard gameplay invariants

1. **E BOARD:** a verified original C++/SQLite `PERSON(state=0)` permits exclusive ship-writer handoff. Once the original cockpit host independently confirms initial `POSE` and `SHIP`, the game asks for exactly one journaled native `SEAT` if not already seated. No auto-seat success is claimed before the native acknowledgement. No second SQLite writer or Unity/Godot state substitute.
2. **No in-ship exploration:** the Godot input controller receives native boarded custody and **never emits cabin MOVE** during that state, whether SEAT is pending, confirmed, or a docked departure/exit is in progress. The only ship input while seated goes to the existing original C++ pilot-command pipe. Seat refusal is fail-closed, not a request to let the client wander.
3. **Third-person always:** ship camera remains exterior at **all** native aboard/dock states. V cannot toggle a cabin view; no local free-look grants movement or control authority. The visual hull follows verified original ship heading; inertial dots and eased camera are display-only and have no physics, ore or collision.
4. **E while flying:** does **not** request SEAT=0 and cannot unseat or disembark. U/G docking and original native authorization are unchanged. E while initially docked after boarding also cannot unseat.
5. **Single-E disembark:** only after an original physical in-space→docked transition authorizes return-to-colony (not a client-side V toggle or synthetic event), pressing E sets one one-use intent. The cockpit privately requests an original **SEAT=0** receipt *only to transfer to the station host*; the game does **not** show a walkable cabin. On independently verified seat release, the cockpit worker quits, a supervisor/PID single-writer handoff occurs, and a fresh native station `PERSON(state=0)` authorizes the original `EXIT`. Only a committed `PERSON(state=4)` restores on-foot colony camera and WASD walking.
6. **Failed exit/seat:** any refusal, stale/unauthorized receipt, pending request, incomplete dock, worker crash or absent station retains authoritative custody and third-person view. It does not optimistically mark the player back in the colony.

## Acceptance

- Real Godot scene renders **third-person at dock AND in space**, with cabin hidden/blocked and no V input necessary; colony restores first-person only after C++ `PERSON(state=4)`.
- Genuine Windows/Linux native scene follows `colony→12 original WALK→E BOARD→native auto-seat→restart same SQLite→U undock→E cannot stand→G redock→single E disembark→original colony PERSON` with supervised handoff, single writer, no duplicated native seat command and preserved immutable seat receipt.
- Keep original native collision, rock sensor, pilot steering, SQLite/SGDX supervisor and package tamper checks. Group all acceptance into **one** Windows/Linux gate, not per-commit CI.
- Human-operated Radeon AI Pro R9700 physical gameplay acceptance (including flight feel, camera and docking) is independent; do not promote hosted green as owner acceptance.
- Mining equipment fitting and cargo/refining progression are separate, after player can pilot, dock and disembark reliably.

This rule deliberately replaces the earlier exploratory walkable ship/cabin gameplay concept. Interior models may remain stored as inert developer geometry or film assets but must not be exposed as a player camera or movement destination while aboard.
