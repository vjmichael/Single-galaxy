# Single Galaxy — B4.3 Interactive Simulation and Interpolated Client

**Date:** 2026-10-30  
**Result:** Local Linux interactive data path and headless UI-logic gates passed; Windows graphical acceptance remains open.  
**Origin:** Original game, not EVEmu. No historical EVE client, code, or art used.

## Deliverables implemented

1. `src/interactive_host.cpp`: independent C++17 host processes commands over a bounded stdin line protocol and updates one persistent SQLite world on `steady_clock` fixed steps. No arbitrary player, actor, target, command ID, or tick field is accepted. It emits SGVIEW delta frames only after `PersistentWorld::step()` returns after COMMIT.
2. `live/interactive_relay.py`: local-only HTTP relay (127.0.0.1), one host instance per relay startup. Same-origin anti-CSRF token and rigid JSON schemas for thrust/mine/stop. Two clients connect to the SAME world, including a late join. Reconnect replays bounded frame history, not a newly spawned world.
3. `viewer/interactive.html`, `viewer/interactive.js`: original 2D canvas demonstration, command controls, real-world view stream and feedback that HTTP 202 only means queued.
4. `viewer/interpolation.js`: pure presentation-only interpolation. State of moved entities is smoothed between prior/current authoritative state. Removed entities disappear immediately; cargo/resources and authority are not interpolated or modified.
5. `viewer/contract.js` exposes `applyNext` to hash-verify one incoming frame prior to publishing; avoids recalculating the entire prior stream for each arrival.
6. Linux/Windows **developer** launch helpers; contract, limitations, and integrated tests documented.

## Verified checks (actual execution)

- GCC native release C++ compile: **PASS**.
- CTest: original simulation, persistence/recovery, client contract, mining lifecycle: **4/4 suites passed**.
- Node.js contract/UI smoke/interpolation tests: **20/20 checks passed**.
- Direct C++ host: **26 ticks, 27 SGVIEW frames**, SQLite database produced, automatic mining filled 20/20 and emitted cargo-full at tick 5: **PASS**.
- Python local relay integration: **two independent SSE subscribers receive identical 46-frame stream** of one shared 45-tick simulation, including a second subscriber arriving after commands started: **PASS**.
- Native browser requests `mine`, `thrust`, and `stop` forwarded through allowlist; denied missing Origin, incorrect token, boolean thrust, excessive mining rate, and injected issuer field. Command after host termination returns HTTP 409: **PASS**.
- Same-world reconnect after completion supplies same bounded frame history, not a new host: **PASS**.
- Long stream: **180 ticks / 181 frames**, JS contract validation intact; ship cargo 20 and asteroid 20, resource total 40: **PASS**.
- Clang undefined-behavior sanitizer: four original suites **4/4 passed**, interactive end-to-end command/replay tests **passed**.

## Explicit limitations

- Actual headless Chromium rendering test was **attempted and blocked by environment policy**: `net::ERR_BLOCKED_BY_ADMINISTRATOR` when opening the loopback test server. This is **not a verified visual rendering pass**. The UI implementation and data/controller behavior have only programmatic checks.
- No native Windows build or Edge/Chrome acceptance test was performed. Windows helper script is unverified.
- Local development only: anti-CSRF token is not remote multiplayer authentication; any process running as the same trusted local user can potentially obtain the token, and anyone sharing that local browser origin can issue commands.
- Demo host is one hardcoded player ship, asteroid and station. 180 ticks default. SQLite is in a temporary directory and deleted when relay shuts down. No production save slot, robust cross-session restart/resume, unbounded history, network transport, per-client interest management or user permissions.
- This host does not provide a no-Python Windows installer. The relay is a testing helper, not an official game launcher.
- Millimeter-per-logical-tick movement and demo tick milliseconds remain intentionally uncalibrated physical units.

## Next acceptance gates

- Native Windows build/test with verifiable SQLite package; actual Edge/Chrome visual acceptance.
- Server-generated **current-tick snapshots** for bounded-memory resync, not replay from tick 0; bounded output queues and backpressure.
- Durable named save slots and recoverable restart, plus long-run scheduler overrun and latency benchmarks.
- NPC economy prototype only after the interactive and persistence boundary is stable.
