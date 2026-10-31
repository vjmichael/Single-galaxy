# Single Galaxy — B6.2b Durable Inter-System Travel

**Date:** 2026-10-30  
**Scope:** Original space-game authoritative C++/SQLite simulation, developed from B6.1+B6.2a.  
**Status:** Implemented and tested on Linux. **Not yet integrated into the live multi-system browser.**

## Outcome

A ship is an individually owned, durable entity with one `system_id` in a **single SQLite-committed authoritative world**. The v5 simulation now registers multiple star systems, adds persistent gate endpoints, and moves a ship from the source partition to the destination partition during a two-tick transfer without changing its entity ID, owner, or cargo. Stations, markets, asteroids, and hangars retain their original system assignments. There is no two-database handoff or temporary duplicate of a vessel.

The existing `PersistentWorld` transactional approach is retained: clone candidate state, simulate, write audit/receipt and canonical snapshot inside one SQLite transaction, and only then publish the in-memory state. A failure before COMMIT leaves the original state unchanged. The same system-independent ore and credit accounting remains active.

## Implemented

1. **Simulation snapshot v5:** Registered `system` records; system-aware entities; `Stargate` entities with source position, destination system and arrival coordinates; ship `Ready`/`Jumping`/`ArrivalCloak` phases and countdown. Strict canonical reconstruction rejects invalid system IDs, bogus gates, contradictory ship states, and duplicate entities. Previous v1–v4 snapshots remain readable.
2. **Game operations:** Owner-authorized `RequestJump`, correct-source-system and gate checks, a prototype 2,500 **millimeter** activation radius, no concurrent jump, no jump while docked, two simulation ticks of transfer, arrival protection lasting five ticks, movement breaking the cloak. The origin ship vanishes from that system's entity partition and appears in the destination system without duplication.
3. **Physical continuity:** Cargo remains attached to the same ship. Remote asteroid interactions and station-docking requests are rejected even when their 2D coordinates are close. Local markets and local hangars remain separate. Existing mining and credit conservation checks apply across all systems in the same world.
4. **Durable jump journal:** SQLite schema version 4 adds `jump_ledger`. A successful departure records command, ship, gate, source, destination, cargo and accepted tick; the completion tick is recorded when arrival commits. An index prevents multiple simultaneous pending jumps for the same ship. At restore, journal rows are cross-checked against authenticated command receipts, entity/gate definitions, and recorded arrival activity; missing or contradicted rows are rejected.
5. **Explicit recovery:** Tested `PersistentWorld::recover_copy`: corrupt latest snapshot is rebuilt from validated retained checkpoints and operation log into a **new file**, with the corrupt original unchanged.
6. **Developer demo:** `galaxy_multisystem_demo NEW_DATABASE_PATH` persists a loaded ship in system 1, reopens its world during the jump, completes arrival in system 2, docks, unloads and sells ore, reporting retained resources and credits.

## Focused behavior example

```
BEFORE_JUMP system=1 cargo=20
ACCEPTED system=1 protected=1
AFTER_JUMP system=2 cargo=20 protected=1
FINAL system=2 cargo=0 credits=100 ore_total=30 credit_total=2000
```

The focused test adds a **return trip** from system 2 to system 1, asserts the ship exists exactly once, and validates that only its system partition changes. It also covers another owner's attempt to issue the jump, wrong-system gates and mining targets, stale/duplicate command IDs, denial during arrival cloak, recovery on restart, rollback before departure and on transfer commit, journal tampering and deletion, and recovery-copy behavior.

## Verification (Linux, this execution)

| Check | Result |
|---|---|
| Full CTest regression | **13 / 13 pass** |
| JavaScript protocol/UI regression | **33 / 33 pass** |
| B6.2b durable jump tests | **Pass**, including roundtrip, restart, ledger tampering, abort/retry, recovery-copy |
| Clang AddressSanitizer + UndefinedBehaviorSanitizer focused test | **Pass** |
| Fresh persisted demo | **Pass**, 20 ore delivered, 100 credits earned, 30 total ore and 2,000 total credits conserved |

Evidence: `docs/B6_2b_ctest_output.txt`, `docs/B6_2b_node_output.txt`, `docs/B6_2b_sanitizer_result.txt`, `docs/B6_2b_demo_trace.txt`, `tests/MultiSystemTransitTests.cpp`. All earlier B5 and B6.1 suites remain in the project.

## Explicit boundaries and open items

- **B6.2c remains open:** The live `galaxy_interactive_host`, Matrix Vision SGVIEW v1 and browser still operate on one system. The new B6.2b kernel is integrated with `Simulation` and `PersistentWorld` but **not** with that live client process. `ClientContract` intentionally expects a single-system projection; it must gain strict per-system views, new stream generation IDs and scope fencing before rendering jumps. Do **not** send a full multi-system `entities()` map to clients, because that would disclose unseen objects and scanning discoveries.
- The established B6.2a `GateTransit` kernel includes a server-side weapon lock and gate outage. B6.2b's new persistent `RequestJump` flow does **not yet** have durable admin-controlled gate outages or weapon-lock inputs; a gate initialized in B6.2b is online. These need properly authorized game commands and tests.
- `protected_from_attack` is an authoritative **status flag**, not a working combat damage/targeting prohibition. Combat isn't implemented yet.
- Arrival coordinates are deterministic and bounded; no randomized spawn dispersion, inter-system warp animation or gate-camp mechanics are claimed.
- The implementation uses one SQLite database and one in-process world map for all system partitions, not separate server processes or distributed transactions. Real multiplayer authorization, remote sensors, and online global survey database remain future work.
- The test activation radius is 2,500 **mm**, not an assertion about EVE Online gate activation distance. Logical tick duration is not real-world seconds.
- This implementation is original C++ code. No EVEmu code, assets, or proprietary EVE client content were used or modified. Windows-native build and actual Chromium GUI rendering were **not** verified.

## B6.2c priority

Implement system-aware, player-scoped SGVIEW snapshots and resynchronization after a committed jump; prevent stale pre-jump frames and hidden-site disclosure; add authorized jump UI and controls; update live directional and probe scanning to use the **ship's current** `system_id`. Then repeat the full station-to-station-with-jump lifecycle via the real local relay. Retain offline-first persistence and the ledger checks already completed here.

## Reproduction

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
./build/galaxy_b62b_durable_jumps_tests
./build/galaxy_multisystem_demo /tmp/galaxy_b62b_fresh.sqlite
```

For the demonstration use a **new** SQLite filename; the demo refuses to overwrite an existing database. Sanitizer-focused check (Clang toolchain present):

```sh
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude src/Simulation.cpp src/Persistence.cpp tests/MultiSystemTransitTests.cpp \
  -lsqlite3 -o /tmp/galaxy_b62b_sanitized
ASAN_OPTIONS=detect_leaks=1 /tmp/galaxy_b62b_sanitized
```
