# ECON-02B2d4c3 — Native localhost factory construction runtime

**Date:** 2026-10-31 (continuation of EvEmu Convo 06)
**Project:** Single Galaxy, early Linux prototype
**Exact source baseline:** `SingleGalaxy_ECON02B2d4c2_Live_Construction_Source_Tests_2026-10-31.zip` from `/Single Galaxy`; this report describes changes *on top of* that archive.
**Engineering goal:** eliminate the Node.js runtime prerequisite for the trusted factory editor control path, without weakening host authority, construction/material accounting, or restart recovery.

## Delivered code

- `src/factory_native_host.cpp`: new C++17 local host executable `galaxy_factory_local_host`. It owns one actual `PersistentWorld` / `HostBlueprintRegistry` within its own process and serves the existing editor assets and the same narrow HTTP API, with **no Node.js process, subprocess relay, npm package, or external network service** at runtime.
- `src/factory_host_protocol.hpp`: the original, unchanged native `HELLO`, `LIST`, `REGISTER`, `STATUS`, `STEP`, `CANCEL`/SQLite-authorized operation core factored from the old bridge. `galaxy_factory_host_bridge` remains available to exercise compatibility with the previous ECON-02B2d4c2 development server.
- `viewer/factory-live.js`: after a real host connection, makes the status visibly identify the native colony and hides an unsupported return-to-viewer link; offline editing remains available.
- CMake: builds the new native binary, packages the viewer assets beside the executable, provides an install rule for `bin` + `share/single-galaxy/viewer`, and links Windows SDK `ws2_32`, `bcrypt`, `shell32`, and `comdlg32`. On Windows, launching the native program without `--db` invokes an OS-owned **open-existing-save** dialog instead of creating a game state.
- `tests/ECON02B2d4c3NativeHostTests.cjs`: HTTP/SQLite integration regression tests run **against the C++ native executable**, including portable-copy runtime asset lookup, nonexistent-world refusal, origin/host/session denial, invalid blueprints, 4 material-backed construction steps, persisted restart, a fifth step, durable cancellation, denial of cancelled plan execution, and continuation with a replacement plan.
- `scripts/factory_native_browser_smoke.py`: optional real browser-to-native loopback acceptance script, kept as a test even though the present container prohibits Chromium loopback navigation.

## Security and construction contract

- Binds only `127.0.0.1` on a kernel-assigned ephemeral TCP port; Windows asks Winsock for exclusive address use. No remote interface and no CORS. A launch-specific 256-bit cryptographic session secret is produced using Linux `getrandom` or Windows `BCryptGenRandom`; no reliance on the standard C++ PRNG for authority.
- Static assets are an exact four-file allowlist; routes are under `/session/<secret>/`. Responses prevent framing, referrer disclosure and caching and carry a restrictive Content-Security-Policy. The server checks exact `Host`, `Origin`, `X-SG-Session`, and, where present, same-origin Fetch Metadata on writes.
- HTTP/1.1 headers, JSON bodies, length and command ID are strictly bounded and validated. It rejects transfer-encoded requests, duplicate headers, unexpected JSON keys, unknown routes, and invalid IDs. Only host-side code selects the world file, founding ship, outpost, owner, journal IDs and resource changes; browser-supplied `owner` or `site` are not accepted.
- Host doesn't generate a world or silently seed NPCs. Existing non-empty SQLite save and constructed outpost are prerequisites, and multi-outpost worlds still require a trusted `--site` choice. A successful plan step uses existing durable construction and physical inventory receipts; cancelling a plan leaves installed equipment in the game and cannot fabricate a refund.
- The session grants *local launch-operator authority*, not true remote player authentication, sandboxing, or robust production multiplayer identity. Keep the capability URL private; do not open the HTTP port to another network or use through a proxy. No browser is allowed to mint its own tools or authority.

## Developer run instructions

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target galaxy_factory_local_host -j 4
./build/galaxy_factory_local_host --db /path/to/EXISTING.sqlite
```

The host opens the session URL in the default browser, and prints `SG_FACTORY_URL=...` for a manually pasted address. `--no-browser` is intended for test automation; `--site N` selects the outpost only if more than one exists. The CMake build places `viewer/` beside `build/galaxy_factory_local_host`. For a Windows `galaxy_factory_local_host.exe` launched without `--db`, the native save picker requests an **existing** SQLite game world. Windows binary compilation/interactive acceptance has **not** yet been performed in this Linux environment.

`node --test ...` remains used as a **developer test driver** only, not a runtime player dependency. The previous Node-based server remains in source for backwards compatibility and comparison, not part of this new startup path.

## Tests and results — actual continuation execution

| Gate | Result |
|---|---|
| Complete Linux Debug CMake build, all targets | PASS |
| 60 non-live native CTest cases | **60/60 PASS** |
| 26 live/relay native CTest cases | **26/26 PASS** |
| Native factory subset (old and new) | **5/5 PASS** |
| All JavaScript unit tests | **93/93 PASS** |
| Python design tests | **3/3 PASS** |
| Offline Chromium factory editor: save/restore, snapped walls, imports | PASS |
| Native HTTP/SQLite session, packaging, authorization, recovery | PASS |
| Direct Chromium **browser-to-native localhost** visual click path | **BLOCKED BY ENVIRONMENT**: `ERR_BLOCKED_BY_ADMINISTRATOR` on loopback URL |
| Windows compiler + graphical acceptance | NOT RUN |

The initial combined CTest run was interrupted by the execution limit after a partial pass. The full set was then run as **two exhaustive, disjoint selections** (60 non-live and 26 live/relay), both passing. Don't describe this as one uninterrupted 86/86 run. Browser restriction is separate from the native HTTP test, which truly exercises SQLite writes; it is **not** evidence of a full graphical localhost click-through.

## Still open / scope control

1. **Unified game UI integration**: this is a native *factory* host executable, not yet a menu inside the main ship/colony gameplay shell. The game should launch/switch to the editor from the established player session, choose among existing outposts, reconnect and close the host cleanly; never allow two competing writers on the same SQLite world. Resolve the single-writer problem before embedding this loopback service within the running colony host.
2. **Graphical acceptance**: test with Windows 11/Edge/Chrome and a native runtime on a machine where browser loopback navigation is permitted. Test connect, authorize, build, refresh, cancellation, relaunch/recovery and no-world refusal without a shell prerequisite.
3. **Physical walls / doors / floors**: still ghost overlays. New native commands should persist these as real collision-checked world fixtures, with construction material costs and journaled partial work; don't mistake floor paint or a snapped outline for completed construction.
4. **NPC construction crews**: not implemented. Any later delegation must use job eligibility, workforce/time, resource conservation, host-side authorization, cancellation and offscreen deterministic batching. Never let a browser script become an autonomous tool authority.
5. **Balancing / source routes**: construction-module-per-cell values are prototype placeholders. Integrate salvage, haulage, workforce staffing, power, and realistic material bills before wider gameplay acceptance.

Existing project scope remains: one original fictional galaxy; unbounded freeform factory *design* subject to real geometry/resources rather than arbitrary factory-size caps; five physical machine tiers; deterministic conservative remote production; station-centric flight and finite starting NPC crew. This milestone changes only native factory control transport and asset packaging, not these game-design rules.

**Repository policy:** engineering was performed and validated locally. GitHub remains a source repository/documentation archive, not an external build/test dependency or a separate development hub.
