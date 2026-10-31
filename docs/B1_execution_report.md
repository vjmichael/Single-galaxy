# B1 — Headless Single-System Kernel: Executed Verification
**Date:** 2026-10-30  
**Scope:** Original simulation kernel; EVEmu and historical client untouched.

## Reproducibility
- Host: Linux x86-64, kernel 6.18.57, 5 exposed CPUs, Intel Xeon Platinum 8573C.
- Release build: CMake 3.31.6 + Ninja + GCC 14.2.0, C++17.
- Independent build: Clang 17.0.0, C++17, Debug configuration.
- No external game libraries, database, client or network services used.
- Command: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja && cmake --build build && ctest --test-dir build --output-on-failure`.

## Test record — 2026-10-30

| Test | Observed result |
|---|---|
| deterministic_replay | PASS — identical seeded commands yield identical serialized state |
| reproducible_field | PASS — seeded procedural asteroid generation repeatable |
| inertia_and_fixed_step | PASS — velocity persists without thrust |
| authorization_and_invalid_actor | PASS — wrong owner, unknown actor, invalid thrust rejected |
| transfers_conserve_resources | PASS — capacity-limited mining preserves ore total |
| distance_and_invalid_mining | PASS — outside range, zero amount and missing target rejected |
| duplicate_and_past_commands_rejected | PASS — idempotency and logical-tick policy |
| stable_order_with_reversed_submission | PASS — sequence governs execution rather than arrival order |
| bounded_movement | PASS — position and outward velocity bounded |
| empty_ore_and_bad_spawns | PASS — depletion and out-of-range spawn handled |

**Total: 10/10 PASS on GCC Release and on Clang Debug.** CTest registers these as one aggregate executable test; the executable itself reports ten checks.

## Demonstration run (GCC Release)

```text
tick=1 command=1 status=APPLIED transferred=12
tick=2 command=2 status=APPLIED transferred=0
tick=4 ship_x_mm=750 ship_vel_mm_tick=250 cargo=12 asteroid_ore=68 state_hash=13836236031462257247
```

## Preliminary performance smoke — NOT the B5 benchmark

A 500-tick warmup and 3000-tick timed loop was executed for 1/10/100/1000 ship entities. This only tests fixed-step iteration of inactive, stationary ship objects with no active market, collision, NPC AI, persistence, networking or graphics. Results are nonportable shared-container measurements; scheduling outliers occurred. See `B1_preliminary_tick_benchmark_Linux.csv`. The B5 benchmark must remeasure on dedicated Windows x86-64 hardware and add correctness-under-load, realistic commands and workload isolation.

## Unverified or known gaps

- Windows MSVC compile and execution: **NOT RUN**. Provided script is guidance only.
- Crash recovery/SQLite/journal/transactional persistence: **NOT IMPLEMENTED**.
- NPC lifecycle and market: **NOT IMPLEMENTED**.
- Realistic spatial scale, 3D, gravity, orbit, collisions, travel/docking: **NOT IMPLEMENTED**.
- GPU/client rendering, audio, network sync, authentication: **NOT IMPLEMENTED**.
- Replay across architectures or standard-library/compiler variants: GCC and Clang tests passed on same Linux host; Windows remains unverified.
- Command/event log currently accumulates in memory; retained command IDs are not durable.
- Source-to-binary build provenance is local only. No repository push or Windows installer.

## Status
**B1 headless-kernel demonstration and focused correctness gate: PASS in Linux environment; Windows platform gate remains OPEN.** B0 contract remains a draft. Next milestone B2 persistence and failure-injection.
