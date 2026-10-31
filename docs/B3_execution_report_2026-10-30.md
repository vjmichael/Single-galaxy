# B3 — Client Integration Execution Report

**Date:** 2026-10-30  
**Environment:** Linux C++17 native builds, GCC and Clang 17, system SQLite3, CMake/Ninja.  
**Changes:** New client-facing adapter/strict serializer, client projection state, headless publisher/consumer executables, B3 regression suite. **No changes to EVEmu, Qwen client files or GitHub.**

## Results

| Suite | Checks | Status |
|---|---:|---|
| Original core simulation | 10 | PASS |
| SQLite persistence, recovery, migration | 21 | PASS |
| Mining lifecycle (including user cases) | 15 | PASS |
| B3 client-state integration | 13 | PASS |
| **Total focused checks** | **59** | **PASS** |
| CTest suites | 4/4 | PASS |
| GCC/Linux | All CTest suites | PASS |
| Clang/Linux + UBSan | All CTest suites + two processes | PASS |
| Clang/Linux + ASan/leak checks | All CTest suites + two processes | PASS |

### Key B3 scenarios

1. Typed full snapshot → encode/decode → client reconstruction.
2. Thrust creates changed position/velocity and a command outcome.
3. Ten asteroids with 10 ore each, hold 100: client observes ten removals, cargo 100; SQLite reopen state consistent and old events not reemitted.
4. One asteroid with 20 ore, hold 10: automatic transfer 4+4+2; client receives one cargo-full stop; further ticks transfer zero.
5. Dropped delta: client reports `NeedsSnapshot`, does not silently apply later delta; fresh snapshot repairs state.
6. Duplicate frame: ignored; old stream cannot overwrite restarted stream.
7. Altered projection checksum: rejected before client state mutation.
8. Malformed/truncated/noncanonical text frames: rejected.
9. Injected SQLite failure before commit: world remains unchanged and no frame is published; retry succeeds.
10. Persisted active mining job survives restart and appears in new stream; no duplicate historical notification.
11. Entity removal and `AsteroidDespawned` activity are both represented.
12. Stale old snapshot cannot rewind an updated client.
13. Empty committed ticks retain strictly contiguous sequences.

## Actual two-process run

```text
client tick=0 seq=0 cargo=0/10 ore=20
client tick=1 seq=1 cargo=4/10 ore=16
client tick=2 seq=2 cargo=8/10 ore=12
client tick=3 seq=3 cargo=10/10 ore=10 event=MINING_STOPPED:CARGO_FULL
client tick=4 seq=4 cargo=10/10 ore=10
client tick=5 seq=5 cargo=10/10 ore=10
B3 two-process smoke PASS
```

The `galaxy_client_wire_producer` program owns the SQLite-backed simulation and emits wire frames on stdout. A separate `galaxy_client_wire_consumer` process decodes them from stdin and maintains the projected world. This is an actual process boundary, though **only local standard pipes**, not network sockets or a rendered client.

## Remaining engineering gates

- **B2 Windows verification** remains outstanding; current CMake Windows scripts are unverified on a native Windows host. Self-contained SQLite source bundling requires a verified amalgamation.
- No continuous time scheduler; a caller must advance logical simulation ticks.
- No client interpolation/rendering, camera, UI, or game assets.
- No network encryption/authentication, interest management, loss recovery transport protocol, or client-specific privacy/visibility filtering.
- Initial bridge does whole-map diff and serializes full entity state, suitable for small pilot systems but not optimized for a large galaxy.
- 4 MiB frame cap; large views must later be chunked or partitioned. FNV consistency is not secure authentication.
- SQLite snapshot/operation log compaction and longer workloads still need their own gates.
- No claim of correct multiplayer concurrent access or actual Crucible client compatibility.
