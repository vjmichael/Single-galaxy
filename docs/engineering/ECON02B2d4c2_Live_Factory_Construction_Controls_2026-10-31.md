# ECON-02B2d4c2 — Host-connected factory construction controls

**Date:** 2026-10-31  
**Project:** Single Galaxy, original local Linux prototype  
**Baseline:** ECON-02B2d4c1, GitHub `main` commit `f638fd28d01e839b2d2f5358602282a15b6ba752`  
**Scope:** local host-authorized browser-to-persistent-colony control bridge; not production multiplayer networking.

## What changed

The existing freeform Factory Designer now includes a **Live colony construction** panel. In offline/file mode its controls are disabled; its previous layout, snapped walls and doors, floor paint, native export, and editable saved designs continue working. With a trusted localhost host session the panel presents the actual site, ship, available construction modules and registered plan, and offers **Authorize plan**, **Build next step**, **Refresh world**, and **Cancel remaining**.

The Node.js standard-library-only process `scripts/factory_live_server.js` serves the editor's static files and a narrow JSON API on `127.0.0.1` using a 256-bit random launch-specific session capability. It strictly rejects missing session paths, foreign Host headers, foreign Origin writes, missing session headers, incompatible content types, invalid JSON and oversized blueprints. No permissive CORS; no remote bind; no write-capable endpoint outside the per-session path. The browser cannot choose owner/ship/site, issue a raw world command, mint journal IDs, or directly update SQLite.

The C++ `galaxy_factory_host_bridge` process owns the `PersistentWorld` and `HostBlueprintRegistry`, derives the authenticated player from the founding ship recorded at the selected outpost, and refuses missing/empty SQLite databases or a missing outpost. It accepts a small line-framed protocol (`HELLO`, `LIST`, `REGISTER`, `STATUS`, `STEP`, `CANCEL`) only from its supervising process. Native registry plan enumeration is owner-restricted and verifies immutable plan records before reporting them. Each authorized step gets a fresh host command ID; existing SQLite state, resource ledger and recovery define the actual progress and material effects. Plans remain revocable across restart. Failed/blocked steps are not fabricated successes; they return the native outcome.

Each build step still requires **separate player action**; there is no unattended construction loop or NPC work assignment. Cancelling a plan does not undo previous construction. Unsupported wall, door and floor overlays remain outside canonical `.sglayout` and are not built.

## Local use — development only

Requires a **previously created Single Galaxy SQLite world containing an outpost**, CMake-compiled `galaxy_factory_host_bridge`, Node.js (no third-party npm dependencies), and a browser.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target galaxy_factory_host_bridge -j 4
node scripts/factory_live_server.js --db /absolute/path/to/existing-world.sqlite --bridge ./build/galaxy_factory_host_bridge
```

If the world has multiple outposts, append `--site SITE_ID` to the Node command. Open the printed `SG_FACTORY_URL` in Edge/Chrome, load or design a compatible native blueprint, review the estimate, and explicitly authorize/step/cancel. The server must run under the trusted game owner; don't publish the session URL or expose the service through proxies. **No test fixture is silently installed in the user's world.** For a disposable test database only, the separate fixture target `galaxy_econ02b2d4c2_world_fixture` creates an isolated seeded outpost. Keep development fixtures out of user save directories.

## Verification and acceptance

- Native Linux Debug full-target compilation: **passed**.
- Native full CTest, including existing live/relay scenarios and new host bridge E2E: **85/85 passed**.
- JavaScript tests `node --test tests/*.js`: **93/93 passed**.
- Python design tests: **3/3 passed**.
- New Node.js host HTTP/native/SQLite integration: **passed**; validates origin and header denial, noncanonical/unsupported documents, real material-backed steps, duplicate active-plan denial, restart/recovery, cancellation, replacement plan reuse of previous built equipment.
- Offline Chromium browser: **passed**; verifies snapped walls, editable save/load, blueprint import/export, with hosted control changes present without breaking offline mode.
- **Direct localhost Chromium UI navigation in the test container: not passed/not testable.** Chromium returns `ERR_BLOCKED_BY_ADMINISTRATOR` for `127.0.0.1`, including intercepted routes. Real HTTP E2E is tested independently through Node's fetch; do not describe that as a successful full visual browser-to-host acceptance.
- Windows-native compilation, executable packaging, and GUI acceptance: **not yet tested**.

## Open follow-up gates

1. Package the native host with the desktop runtime so no separate Node.js prerequisite is required for normal Windows players, and exercise the editor directly in that host.
2. Test direct browser GUI-to-localhost click paths on an environment that permits loopback browser navigation; test real colony save selections and reconnection.
3. Add built wall/door/floor world entities with material budgets and snapping checks in SQLite; currently visual annotations only.
4. Add explicit NPC construction crew delegation, labor and scheduling. Do not grant browser NPC/world authorization.
5. Improve plan UX for multi-outpost selection, per-job preview, blocked-step recovery and resource sourcing; make construction costs gameplay-balanced instead of hardcoded temporary module-per-cell values.

**GitHub is source storage, not the development hub.** Local code and tests were completed before any repository publication.
