# Single Galaxy — ECON-03c8a integrated source, accelerated 30-minute game simulation and recovery evidence

**Date:** 2026-11-01  
**Baseline:** ECON-02B2d4c2 complete reconstructed prototype source + locally verified ECON-03c2–c7 modules and the c8a `PersistentWorldBridge`.  
**GitHub source commit:** `78f608808094943f80a4f5c5132e895bfd179db2` (`main` and development branch initially).  
**Additional test-source branch:** `dev/econ03c8a-persistent-world-20261101`.  
**Environment:** local Linux sandbox, 5 visible CPU cores, 5.8 GiB RAM; no Windows GPU, rendering, or gameplay UI.  
**Status:** accelerated 30-minute simulated-time scenario **PASS**. Real-time run was intentionally terminated by SIGKILL after approximately six minutes to exercise crash recovery; **not a 30-minute wall-clock soak pass**.

## 1. What “full-game source baseline” means

The complete recovered ECON-03c8a source bundle has 596 files (~4.04 MB uncompressed source/tests/assets). It is a reconstructed, buildable **current prototype source tree**, not a finished, continuous 3D game. It starts from the last available complete local source archive ECON-02B2d4c2, then composes the independently tested regional modules. Subsequent local ECON-02B2d18d production changes remain unmerged and require a separate reconciliation. GitHub was previously storing some later milestone descriptions without those source bytes; this run published the 28 integrated code changes, not just a document.

## 2. Accelerated 30-minute simulation

Command:

```sh
cmake -S . -B build -G Ninja -DBUILD_SOAK_TESTS=ON
cmake --build build --target galaxy_econ03c8a_soak_runner -j3
./build/galaxy_econ03c8a_soak_runner 1800 5 /path/to/empty/soak_directory --accelerated
```

The harness explicitly defines **5 logical ticks per simulated second** for the test. The underlying game kernel does not yet mandate that wall-clock tick rate. Simulated time = 1,800 seconds = 9,000 persisted game ticks. `--accelerated` disables sleep; it is not a 30-minute wall-clock run.

### Accelerated results

| Metric | Observed |
|---|---:|
| Logical duration | 1,800 s / 30 min |
| Persisted world ticks | 9,000 / 9,000 |
| Actual execution time | 6.2958 s |
| Mining commands / cargo | 30 commands / +300 ore |
| Starting / ending total physical ore | 152,400 / 152,400 |
| Durable `PersistentWorld` restarts | 6 |
| Snapshot reparse checks | 60 |
| Cold-region SQLite checkpoints | 31 |
| Cold cohorts | 512 × 1.3 trillion residents |
| Total represented cold population | 665.6 trillion (aggregated, **not individual NPCs**) |
| Additional supply/receiving colonies | 2 (514 aggregate regional colony records total) |
| Max sampled process memory | 7,720 KiB (~7.5 MiB) |
| Worst measured world-step time | 7.40013 ms |
| Final world state hash | `4178665652225073691` |
| SQLite validation / ore conservation / restart consistency | PASS |

Two supply shipments were scheduled in the separate cold-region simulation. Their cargo custody and checkpoint changes were covered by the existing regional assertions. The live world and cold regions are **two deliberately separate domains** in this harness; they are not live-to-cold reconciled physical shipments.

### Additional microbenchmarks (single-run, not controlled game FPS)

- In-memory ship stepping (3,000 measured ticks following warmup): 1,000 ships p50 17.947 μs; p95 20.561 μs; p99 63.495 μs, worst 358.410 μs. Also measured 1, 10 and 100 ships (see CSV).
- SQLite persistence benchmark with 1,000 asteroids, 500 steps: p50 1.512 ms; p95 3.052 ms; p99 3.980 ms; worst 5.272 ms, total 857.91 ms.
- These runs include no rendered 3D objects, physics collision broadphase, dynamically operating 1,000 factories, or networked multiplayer. They do not establish end-user hardware minimums.

## 3. Real-time paced run and abrupt-failure recovery

Command:

```sh
./build/galaxy_econ03c8a_soak_runner 1800 5 /path/to/empty/realtime_soak_directory
```

This second run was **paced at real 5 ticks/second**. It reached 1,500 ticks (5 min) and successfully reopened `PersistentWorld` from SQLite with an identical hash. The bridge correctly rejected unsafe demotion of a now-mutated detailed world. We deliberately sent `SIGKILL` to the simulation after **6 min 03.60 s** of wall time, **before** the intended 30-minute endpoint, to check abrupt crash recovery.

An independently compiled verifier reopened both SQLite databases and checked:

- restored detailed-world tick: **1,817**;
- ship mining cargo: **70** (the accepted commands were preserved);
- restored cold-region tick: **1,800**;
- recovered regional colony records: **514**;
- original total physical ore: **152,400**;
- SQLite `PRAGMA integrity_check` of both databases: **ok**;
- persistent regional owner remained **Detailed** (not silently reset to Cold).

This is evidence for **six minutes of real-time execution and successful forced-crash restart**; it must not be presented as evidence for a complete 30-minute elapsed wall-clock soak. The accelerated 30-minute simulated-time test above is the completed half-hour simulation.

## 4. Full regression coverage

- Linux full native build: **PASS**, including all historical and regional test targets.
- Native/integration CTest: **all 92 test indices individually passed across completed split runs**. The initial all-in-one run was interrupted by the tool execution limit; it was *not* reported as a successful uninterrupted run. Ranges: 1–42 passed in a partial run, 43–45 separately 3/3, 46–70 25/25, 71–92 22/22.
- Node tests: **93/93 PASS**, rerun on the integrated source.
- Python design tests: **3/3 PASS**, rerun on the integrated source.
- Optional soak runner CMake build (BUILD_SOAK_TESTS=ON): **PASS**, plus 3-second smoke test.

## 5. Unclosed gates and interpretation

1. `PersistentWorldBridge` still fences unsafe **changed detailed→cold demotion** rather than reconciling physical inventory and work effects. Genuine c8b physical-effect/cargo reconciliation remains open.
2. The baseline predates local ECON-02B2d18d manufacturing work; no unverified source reconciliation or replacement of that branch occurred here.
3. The regional harness runs on separate saved data and **does not validate actual NPC-operated ships or route/carrier receipts**.
4. It does not establish that a 665.6 trillion NPC galaxy could be rendered, inhabited by fully simulated agents, or operated at game framerate: populations are aggregated numbers.
5. No Windows-native installed-game, Vulkan, GPU, 3D continuous-universe, multiplayer/network, full 30-minute wall-clock, or visual LOD acceptance was performed.
6. One concurrent full-suite build/load transient drove a **270 ms** worst world-step in the six-minute paced run. It is not a frame-rate benchmark, but host scheduling contention needs profiling in the intended Windows deployment.

**Next development:** ECON-03c8b should reconcile real detailed-world effects to the regional custody ledger with exact source receipts, one authoritative material owner and crash-safe promotion/demotion. The architectural visual direction remains 1960s–1980s industrial space design and silhouette-preserving large-object LOD.
