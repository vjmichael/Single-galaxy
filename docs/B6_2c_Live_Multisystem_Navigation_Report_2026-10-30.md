# Single Galaxy — B6.2c Live Multi-System Navigation

**Milestone date:** 2026-10-30  
**Stage:** Development prototype, Linux verified  
**Base:** B6.2b durable multi-system C++/SQLite simulation  
**Scope:** Connect B6.2b's real per-ship system ownership to the live local client, with validated, scoped view updates and actual gate controls.

## Implemented

1. **Owner-scoped, system-local presentation projection.** `ServerViewBridge(...,viewer_owner=7)` derives the active system from the player's actual persistent ship and projects only entities, stations, station markets, and owned private hangars in that system, plus the owner's wallet. Unknown owners are rejected. The legacy owner=0 test constructor remains usable for historic single-system replay tests.
2. **Stargates in the client contract.** Type 3 (`Stargate`) is accepted and visualized; previously the projector could neither validate nor draw a stargate entity. The browser now labels and renders gate objects using original geometric symbols (no proprietary EVE assets).
3. **Server-owned jump-state telemetry.** Optional canonical `K|ship|state|pending_gate|remaining_ticks` and `L|ship` records give the client verified transition status (`JUMPING`, `ARRIVAL CLOAK`) without changing the model of server authority. C++ and JavaScript validate these updates and include them in the projection consistency hash. Legacy recordings without jump telemetry remain valid.
4. **Full snapshot on active system change.** A ship arrival changes the active projection and issues a fresh SGVIEW v1 full snapshot with a strictly increasing sequence in the same stream. Old entities and local inventories vanish from the client's current-world state. Ordinary steps remain deltas. Client live-bridge accepts only a properly sequenced system-switch snapshot; unexpected or stale snapshots are rejected.
5. **Live controls.** `Jump Gate` and `Assist to Gate` buttons send allowlisted `jump`/`travelGate` requests over the localhost relay. The player/browser cannot choose or forge gate IDs, system IDs, or ship IDs; C++ chooses the gate for the ship's actual current system, validates proximity/authorization, and persists all state transitions. Existing B5.2/B5.3 ship controls and navigation remain.
6. **System-local environmental scanning.** The live host now owns star/planet/hidden-site records in systems 1 and 2, uses the current committed ship system for scanning, rejects scanning during jump transit, and sends a `SYSTEM` sensor event plus major objects on entry. Previously surveyed records stay in SQLite; fresh local observations do not silently carry over between systems. Probe ownership and recovery are preserved.
7. **Roundtrip with physical commerce.** Host scenario: docked ship → undock → mine 20 ore → jump from system 1 into system 2 → survey frontier hidden sites → dock at frontier station → unload and sell ore for 7 credits/unit → undock → jump home. All trade and gate commands run in the authoritative C++ world.

## Verification performed

- `ctest --test-dir build -j2 --output-on-failure` — **16/16 passed**. Test log: `docs/B6_2c_ctest_output.txt`.
- `node --test tests/*.js` — **35/35 passed**. Test log: `docs/B6_2c_node_output.txt`.
- Focused C++ test `galaxy_b62c_projection_tests` — verifies no remote entities/market exposure, owner verification, pending jump state, arrival cloak, full system-switch snapshots, rejecting malicious changed-entity snapshots, returning to the origin and restart.
- Focused real-host `galaxy_b62c_live_multisystem_e2e` — verifies **116 consecutive SGVIEW frames**, two system changes with per-system snapshots, zero cross-system entity leaks, client-side hashing, locally scoped survey contacts, probe recovery, a 20-ore sale, and **140 player credits**. Its input schedule is driven by the host's **committed tick count**, not wall-clock assumptions. Reproducible recording: `recordings/b62c_live_roundtrip.sgview`.
- Local HTTP `galaxy_b62c_relay_gate_controls` — verifies allowlisted commands, rejection of client-specified raw gate targets, token/same-origin boundaries, and **57 authoritative frames** with a real destination snapshot.
- Node UI smoke — verifies live HUD jump states, temporary arrival cloak indication, frontier market price, final wallet, refreshed local survey view, and rejection of out-of-order system snapshots.
- Parallel test note: running five live host tests concurrently caused a B5.3 relay readiness timeout; that test passed alone. A first iteration of the new B6.2c test also had wall-clock command-race failures under contention. We fixed the *new* test to schedule by simulation ticks and ran the final full suite with `-j2` (all pass). Avoid inferring that `-j5` is stable from this result.

## Historical EVEmu report review

User-supplied `evemu_client_server_interaction_report.md` is useful as background on service-oriented RPCs, snapshot/delta lifecycle, client-local asset mapping and the server/client boundary. It **does not establish** actual client-server interoperability for all listed features. Source presence is not runtime validation.

Evidence issues requiring caution:
- Report introduction asserts **48 of 60 (80%)** calls working, but its later table asserts **52 of 60 (87%)**; neither percentage is a validated end-to-end pass count.
- Detailed examples combine placeholder type identifiers and asset paths; they should not be accepted as extracted asset truth. A listed `Vexor` under a `Caldari` asset path is a particularly visible example mismatch.
- Earlier corrected C5 errata explicitly left client interpolation a **hypothesis**; the new report calls it confirmed.
- The report's `client_server_calls.csv` is **structurally invalid CSV**: 68 numbered records were parsed as a single 607-field row because notes have unterminated quotations; #2 also has an unquoted comma in the server module field, and #59–65 omit one column. We preserved the original byte-for-byte in `research/evemu_client_server_calls_original.csv` and generated `research/evemu_client_server_calls_normalized.csv` with **68 valid records / 10 fields each**, retaining the original claimed evidence labels. `research/repair_uploaded_evemu_calls.py` regenerates and validates that normalization. This **repairs formatting only, not the truth of the claims**.

## Limits / future gates

- **Not a production multiplayer privacy or authorization boundary.** The host and relay intentionally serve one local player. Cross-player ship-cargo redaction, genuine identities, proximity-based interest management and protocol authentication are still needed.
- **Real browser rendering and native Windows execution were not tested.** Node DOM/render-call smoke tests and C++/Python end-to-end tests are not graphical browser validation.
- **Local prototype only.** Stargates are fixed predetermined connections, not buildable by players yet. Route planning exists separately; there is no warp travel system, gate combat, ship-to-ship PvP, or NPC personnel simulation yet.
- **Scanning** has basic D-scan/probes and SQLite archive, but no shared online discovery rewards, global survey aggregation, advanced signals, or public-access authentication.
- **Recovered ships and transfers** are durable through the B6.2b SQLite model; the interactive relay's save is still a temporary demo created for that run, not a user-facing named persistent save slot.
- **Materials** are currently simplified integer ore units, not the later mass/volume salvage-and-recycling model.

## Reproduce on Linux

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j2 --output-on-failure
node --test tests/*.js
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Open `http://127.0.0.1:8766/viewer/interactive.html` on the same machine. After undocking and mining, use **Jump Gate** near the local gate; the client projects the destination system. `Assist to Gate` uses real thrust and braking. It does not teleport the ship. Default demo maximum runtime is bounded.

**Next gate:** B6.3 navigation/sensor polish and persistent save-slot controls; longer-term warp/waypoint route execution, player-buildable gate infrastructure and scanning data economics. Prioritize real-browser/manual Windows validation when a compatible environment is available.
