# B5 — Station spawn, dock/undock, and location-bound hangar tests

**Date:** 2026-10-30  
**Scope:** Original Single Galaxy C++17 simulation / SQLite-backed world. **Not** EVEmu game code.  
**Status:** The requested two headless acceptance scenarios and eight additional station invariants pass on Linux. Native Windows and actual B5 browser UI remain open gates.

## Requested behavior

- The **game-start** path uses `spawn_ship_docked(owner, station_id, ship_capacity, hangar_capacity)`; the player's ship begins docked at its home station. The older `add_ship` free-flight constructor is retained strictly for existing B0–B4 fixtures and is still used by the preexisting B4 interactive development demo.
- `RequestUndock` is checked and accepted by the authoritative simulation. The ship transitions from `Docked` → `Undocking` → `InSpace` automatically after **two additional committed simulation ticks**. It appears 1,500 mm outside the station.
- Ship thrust and inertia are unchanged. Mining is disallowed while docked or during transitions. Nearby asteroids can be mined without a warp jump.
- `RequestDock` requires an existing station within 2,500 mm of the ship and **zero current velocity**. A valid request transitions `InSpace` → `Docking` → `Docked` after two additional committed ticks.
- Every station permits first-time docking by allocating an **empty, owner-specific** hangar with default capacity 1,000 ore units; the explicitly configured game-start hangar can have a different capacity.
- `UnloadOre` is only permitted once **fully docked** and only into `hangars[(current_station_id, player_id)]`. The transfer is bounded by requested amount, ship cargo, and remaining **local** hangar capacity. Excess ore remains on the ship.
- The sum of asteroid ore + ship cargo + every station hangar's ore is conserved. SQLite snapshot version 3 and the transfer ledger persist the new states, with verified command receipts and rollback protection.

## Scenario A: unload everything successfully

Initial values: Station A at x=0, remote Station B at x=30,000 mm, ship hold 20, Station A private hangar capacity 25, asteroid at x=7,000 mm containing 15 ore units. The mine range is 5,000 mm; the ship must leave the station and accelerate into range before mining.

**Observed PASS**: ship started Docked; an attempted mining command while docked was rejected; undocking completed; ship moved out to x=2,000 mm, stopped by reverse thrust, auto-mined 15 units, and the asteroid despawned; the ship returned to x=1,750 mm with zero velocity, requested docking, and completed docking; an attempted unload to remote Station B was rejected. The valid unload transferred **15** units from ship to Station A's private hangar. Final values: ship cargo **0/20**, local Station A hangar **15/25**, remote Station B hangar absent, total world ore **15/15**. Results survived closing/reopening SQLite.

## Scenario B: station hangar smaller than ship cargo

Initial values: ship hold 20, private hangar capacity 7, asteroid 20 ore units. The same undock → travel → mine → return → dock sequence completed.

**Observed PASS**: ship mined 20 units and returned. `UnloadOre(100)` committed a **partial transfer of 7**; Station A hangar reached **7/7** and ship retained **13/20**. A second unload reported `HANGAR_FULL` and transferred zero. Total ore remained **20**. Results survived SQLite reopen.

## Additional checks

1. Reject other-player commands, mining/thrust during dock transitions, unloading outside a dock, and commands targeting the wrong station.
2. Reject docking while moving, and a station too far away.
3. First docking at a second nearby station creates an **independent empty** owner hangar; moving six ore into it does not affect the five-unit-capacity hangar at home.
4. Reopen SQLite while an undock/dock transition is in progress and complete it without repeating the user command.
5. Inject a failure **after the would-be snapshot write but before COMMIT**; both ship and hangar remain unchanged and retry succeeds exactly once.
6. Deliberately damage the latest SQLite snapshot, reject normal load, and use `recover_copy` to reconstruct a separate copy by replaying the operation journal. The original remains corrupted and untouched.
7. Reject a snapshot with a hangar attached to a nonexistent station; reject a tampered durable transfer ledger that names the wrong station hangar.
8. Send station command results, undocking/docking lifecycle activities, asteroid removal, and unload transferred-amount receipts through the existing B3 client-state serialization, then verify the separate client receives them.

## Test execution

- GCC Linux Debug: **5/5 CTest suites passed**, including **10/10 B5 station tests**.
- Clang Linux AddressSanitizer + UndefinedBehaviorSanitizer: **5/5 CTest suites passed**.
- Original Node/browser contract tests: **20/20 passed** (using old recorded B4 fixtures).
- B4 interactive host/relay and B4 live relay process integration passed unchanged.
- **Clean extracted release archive:** rebuilt successfully and passed **5/5 CTest suites** and **20/20 Node/browser contract checks**, with all three recorded live-stream fixtures included.

`tests/StationLifecycleTests.cpp` contains exact numeric assertions. Run `galaxy_station_lifecycle_tests` to reproduce the two primary scenarios and eight targeted checks.

## Deliberate limitations and future gates

- This is a **headless station-lifecycle simulation**. The station states and owner-private hangars exist authoritatively in C++/SQLite, but the B4 browser currently does **not** display the private hangar or station interior, nor does its existing command UI offer Dock/Undock/Unload controls. The core client message adapter understands the new command results and completion notifications, but a **versioned client projection** is needed to display full dock-state/hangar quantities safely.
- These are discrete simulation ticks, **not a separately verified wall-clock docking UI sequence**. The existing B4 real-time host provides a fixed-step scheduler but still boots its legacy free-flight fixture; wiring it to station spawn and new commands is an explicit next gate.
- One star system, two-dimensional integer positions, simplified docking distance and zero-speed rule; no warp/jumps or multi-system travel yet. The displayed screenshot is historical EVE reference only; no EVE UI/asset/code was used.
- Hangar inventory currently tracks only **one ore resource count**. It does not yet implement item stacks/types, physical volumetric cargo, ships stored as items, market interfaces, contract transport, wallet/ISK, escrow, or NPC haulers.
- No station slot congestion, NPC approval queues, undock traffic management, player creation after initial boot, station ownership permissions, or real market access yet.
- Native Windows x86-64 build/run and installer are **not verified** by the Linux tests.

## Next proposed gate

**B5.2 Live Station Client:** Connect the B4 shared host and browser controls to game-start docked spawning, `RequestUndock`, `RequestDock`, `UnloadOre`, and a versioned presentation snapshot that includes player-specific hangar quantities. Run live reconnect/serialization tests, then native Windows acceptance.
