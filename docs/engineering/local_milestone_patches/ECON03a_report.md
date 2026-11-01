# ECON-03a: Single Galaxy resource-only host and MMO-compatible separation

**Date:** 2026-11-01  
**Baseline:** `SingleGalaxy_ECON02B2d18d_Shared_NPC_Duties_Source_Tests_2026-11-01.zip`  
**Artifact:** `SingleGalaxy_ECON03a_Resource_Only_Host_Source_Tests_2026-11-01.zip`  
**Scope:** Active resource-only economy and explicit legacy regression fixture; no wages or premium currency as a game mechanic.

## Results

- New interactive session starts without markets, initial wallet entries, or sale capability, and rejects raw SELL input.
- Physical mining and cargo load/unload remain authoritative. No currency minted in default station demo.
- The more representative colony-start host already uses physical resources and NPC needs; it is unaffected by this change.
- Legacy station-market fixture is opt-in through a QA-only build switch so archived save/wire tests remain executable. Production builds compiled without SG_QA_AUTOMATION reject the legacy flag. The old financial types and credit journals are still in the kernel for migration; complete removal remains future work.
- Authenticated relay rejects sell in normal sessions; hidden historical UI is revealed only when legacy fixture mode is explicitly enabled.
- New code-level acceptance: `/mnt/data/sg_work_20261101/single-galaxy/tests/ECON03aResourceOnlyHostTests.py` checks normal + fixture direct world state, default-session sell-command rejection via HTTP, authentication, and local save creation.
- Design direction documented: single-player-first, **invite-only multiplayer** (not yet implemented), no public matchmaking, no currency/wages, and **potential future MMORPG foundations without prematurely engineering servers**.

## Verification

- Updated local `galaxy_kernel` + interactive + colony executable targets compiled successfully with CMake/Ninja (Release Linux).
- Targeted CTest **7/7 passed**: kernel, B52 station end-to-end, B53 logistics kernel and relay, new ECON03a resource-only host, B62c client projection, B64 region.
- JavaScript Node tests **93/93 passed**.
- Historic B53 market fixture direct and relay accepted with explicit developer flag; no default market behavior restored.
- Attempted full all-target build only reached 52/188 recorded steps before the command execution window. **Not a verified complete build**, and no full CTest or Windows graphical acceptance is claimed.
- No invite-only session transport, global MMO sharding, or modern 3D gameplay was built in this increment.

## Next gates

1. End-to-end physical NPC deliveries to arbitrary generic machine input ports, preserving physical inventory and worker custody.
2. Legacy money schema isolation/migration, without corrupting previous saves.
3. Invite-only session design and host authorizations after the core solo loop is playable.
4. Original visual vertical slice: ship, mine, deliver, dock and construct.

GitHub was not altered; repository remains a publishing and history target, not the primary development environment.

## File-level source delta

- Base files: 638; resulting source files: 641.
- Files added or changed: 15.
- `CMakeLists.txt`
- `docs/design/RESOURCE_ECONOMY_INVITE_ONLY_AND_FUTURE_MMO_2026-11-01.md`
- `docs/engineering/ECON03a_RESOURCE_ONLY_HOST_2026-11-01.md`
- `include/galaxy/Region.hpp`
- `include/galaxy/Simulation.hpp`
- `live/interactive_relay.py`
- `src/Region.cpp`
- `src/Simulation.cpp`
- `src/interactive_host.cpp`
- `tests/B53InteractiveEndToEndTests.py`
- `tests/B62cLiveJumpTests.py`
- `tests/ECON03aResourceOnlyHostTests.py`
- `viewer/interactive.html`
- `viewer/interactive.js`
- `viewer/viewer.js`