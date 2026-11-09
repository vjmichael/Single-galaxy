# FP3D-WIN-01 — actual Windows native game, on-screen 3D and durable state

**Owner requirement:** Compile with genuine MSVC, run SGDX-protected original C++,
Godot 4.7.2, SQLite/MDMP tests, verify the delivered Windows package, then
prove real **physical PC** camera, asteroid, docking, recovery and one original
saved-world lineage. Keep equipment fabrication and in-game mining deferred
until the 3D world is accepted. A queued workflow or source contract is not
acceptance evidence.

## Required evidence tiers (not interchangeable)

| Evidence | What is verified | What cannot be inferred |
| --- | --- | --- |
| Native Linux Debug and Release 18/18 | Real GCC, original C++/SQLite, host custody, physical collision, one-tonne ore ledger/refinery and tests | Windows MSVC, Windows graphics, minidumps |
| Windows GitHub Actions | Genuine MSVC x64 kernel/hosts, Windows SGDX Job Object and optional private MDMP/ACL, Godot parser and C++ child, ZIP re-extraction/PE64/SHA | Owner's installed GPU, physically observed usability |
| Windows GitHub **non-headless** Godot | Original C++ seeded world, rendered viewport PNGs, verified native camera/dock/collision/restored SQLite; fails if no real window | Human camera quality, physical GPU driver, extended real gameplay |
| Creator's **physical Windows PC** | Actual interactive 3D visuals and usable flight/camera, dock/exit, restart, frame pacing and evidence/approval | Long-term 3D performance or manufacturing not yet tested |

**Always bind results to the exact PR #125 commit SHA** in
`ci/task-group-request.json`. A precompiled ZIP is an artifact only after its
MSVC/native/headless tests and the *actual packaged ZIP validator* pass; a
windowed-GitHub failure must remain visible even if a package exists.
The operator's physical GPU playtest is never auto-approved by GitHub CI.

## 1. Native Windows group gate

Use the **single** central
[SG task-group acceptance workflow](https://github.com/vjmichael/Single-galaxy/actions/workflows/ci-task-group-acceptance.yml),
not per-file or per-commit CI:

1. Authenticator checks current same-repository PR #125 head equals requested
   40-hex source SHA; do not reuse results from superseded heads.
2. MSVC x64 with SQLite vcpkg builds original kernel, cockpit/station/observer
   native hosts, SGDX supervisor and fault child, native collision and refinery
   tests. On Windows actually run the bounded process/pipe/SQLite CTest group
   and verify private MDMP data/NTFS ACLs under an opt-in fixture.
3. Real Godot 4.7.2 headless parser, original host/collision/dock test, and
   native-source ingestion must exit zero. All native gameplay writes remain
   through `PersistentWorld` with the sole SQLite writer lease.
4. Package four x64 native hosts, SQLite x64 DLL, SGDX SHA manifest, original
   Godot scene and the **official Godot 4.7.2 Windows executable** inside
   `SingleGalaxy_FP3D_SGDX03_Windows_Precompiled.zip`. The workflow pins
   upstream official Godot `Godot_v4.7.2-stable_win64.exe.zip` SHA-256
   `731980f9608d61333e5baf54a2ef17210acc7a538446c0cb9969f002aca1e953`
   before extracting it, then copies the actual PE64 engine into
   `desktop_godot/runtime/`. The package includes its own executable
   SHA-256 manifest and the **upstream MIT license**. Do not depend on a
   separately installed Godot editor, internet download or OS file picker.
   `Verify-PrecompiledPackage.ps1` re-extracts the delivered ZIP and rejects
   wrong PE machine/format, missing/mismatched bundled engine SHA-256/license,
   wrong source SHA, missing native or scene files, and private `.dmp` or
   `.sqlite` data. Windows signing remains deferred.
5. In a **non-headless** on-screen Godot process, seed a separate original
   finite-asteroid SQLite world and execute
   `fp3d_env01_live_scene_acceptance.gd`. The actual window must generate
   independently hashed and nonempty rendered viewport PNGs:
   `01-docked-cabin.png`, `02-exterior-flight.png`,
   `03-native-rock-impact.png`, `04-restarted-world.png`.
   `Test-WindowedNativeGodot.ps1` checks native dock, yaw/clearance, collision
   event and stopped velocity, absence of replay on restart, exact original
   tick/generation/position/ore continuity and distinct rendered frames.
6. Upload ordinary logs/CTests, screenshot PNGs and evidence JSON.
   **Never upload Windows minidumps or a private player SQLite save.**

If the hosted Windows process lacks a real display, report a **windowed test
environment blocker**. Do not substitute `--headless` or synthetic frames
and label that a physical GPU pass.

## 2. Actual installed Windows 10/11 physical playtest

After a **real** Windows package artifact exists, extract it to a writable
folder. Do **not** use an old ZIP or build from source on the user's PC.
Double-click `desktop_godot/RUN_STATION_PLAYTEST_WINDOWS.cmd`. The launcher
checks native hosts, SGDX, exact source SHA and the **bundled** Godot 4.7.2
runtime's SHA-256 and version, then runs the actual packaged scene's Godot
smoke tests and opens the window. The default path uses the included editor
executable strictly as a game engine. No Docker, Python, Visual Studio,
CMake, separate Godot installation, file picker, compile or download is
required on the owner's PC. The optional `-GodotExe` PowerShell argument is
an explicit developer override and must not count as the bundled-package gate.

Check the following **in order** in the actual window; F11 is optional
explicit capture of **the game viewport only**. Each F11 screenshot has a
`operator-viewport-receipts.jsonl` record containing the SHA-256 of the PNG
and a real original station-person receipt or matching SHIP/ROCK observation.
All captures are automatically added to
`desktop_godot/SingleGalaxy_Station_Playtest_Evidence.zip` on exit.
The application does not record the Windows desktop.

1. Press **H** for original station mode. Verify visible floor, corridors and
   hull at expected scale; **WASD** reaches the actual berth and the **R**
   refinery terminal. Check that remote terminal access is denied until the
   physical original station-person is near enough; F11 captures station
   evidence. No ore is created by a visible prop.
2. Press **E** near the real hatch to board the **existing owned ship**, then
   **P** to hand the sole original SQLite writer to the cockpit host. Press
   **E** at its actual cockpit to sit. Verify no second writer, no Godot
   teleport, and native seat status. Use F11 for a docked cabin screenshot.
3. Press **U** to undock, then **V** for the 3rd-person exterior. **Up/Down**
   thrust, **A/D** yaw, **W/S** strafe and **B** counterthrust. Verify exterior
   camera behind and tracking the native hull, real asteroid depth and heading
   updates, with no sudden world-coordinate jump. Capture F11.
4. Approach the **finite original test asteroid**. A solid impact must stop
   the native ship, show an **ORIGINAL SOLID CONTACT** HUD event, keep the mesh
   outside its conservative real hull radius and never mine/free any ore.
   The asteroid mesh, cockpit range/bearing instrument and 3rd-person camera
   must agree by eye. Capture F11 on impact, including the original rock/tick.
5. Navigate back to the actual station. Reduce speed with **B**, then **G**
   to dock when physically within the original docking envelope. Confirm
   camera and cabin restore, then **P** / **H** handoff to the station host.
   Press **E** to disembark from the correct berth. No dock from distant space,
   no second original SQLite writer and no invented replacement hull.
6. Close the window cleanly, launch the **same** extracted game again and
   repeat **H/P** as appropriate. The same original ship, geological source,
   position, finite ore/cargo and journal generation must resume. Previously
   committed impact must **not** be replayed as a fresh event. Capture F11.
   Also test abrupt application close after a clean checkpoint: restart may
   not duplicate cargo, a ship, an ore lot, or refund losses.
7. Observe actual frame pacing, shadows/geometry and camera occlusion on the
   creator's real GPU. Any visual overlap, inverted input, stutter, crash or
   missing frame must be recorded **as a failure with step and timestamp**,
   not "looks fine" by inference.

**Controls are deliberately temporary** (no configurable controller/key mapping
at this stage). The current physical world test permits no production drill,
fitting/crafting, free ship replacement or invented ore.

## 3. Acceptance decision and continuation

The player-visible state is **PASS** only after (a) current-head real Windows
native/MDMP/SQLite and Godot tests, (b) real package verification, (c) windowed
engine viewport/receipt checks where a renderer is available, and (d) the
physical PC owner records approval with relevant screenshot receipts and no
unresolved behavior failures. A mere zero-exit window is not sufficient.

*Reject/repair* with the exact failing command, Windows/Godot stdout/stderr,
PR commit, original SQLite journal result and frame receipt. Preserve the
physical world and do not auto-replay a rejected native operation.

**Only after the 3D gate passes:** integrate a genuinely manufactured, fitted,
powered and source-provenanced mining tool; use the existing original C++
`Mine` command to debit exactly one tonne, then verify cargo, docking,
`UNLOAD`, tagged station lot, `REFINE`, 1000kg named minerals and restart on
the same Windows installation. PR #124 remains experimental until that time.

## 4. FP3D-WIN-02 — self-contained player runtime (subsequent task)

**Source implementation:** PR #125 now stages a bundled, official MIT-licensed
Godot 4.7.2 Windows x64 executable with SHA-256 verification alongside the
compiled SGDX/C++ game hosts. The double-click launcher defaults to the
bundled runtime; no editor picker, PATH fallback, downloading Godot or
software build tooling. A user-specified `-GodotExe` exists only for developer
diagnostics and is clearly marked as **not** package acceptance.

Acceptance additionally requires executing the actual precompiled ZIP's
`RUN_STATION_PLAYTEST_WINDOWS.cmd` on Windows with no installed Godot.
A corrupt or missing engine, license or digest must fail closed rather than
silently selecting a system editor. A zero-exit window is still
`WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION`, never automatic physical GPU
approval. A future NSIS-installed distribution remains a *separate*
packaging/delivery enhancement; this ZIP is a self-contained alpha playtest,
**not** an NSIS-installed application.

**Evidence status:** source/CI packaging and validation contract committed;
no current-head Windows runner build or self-contained real-PC launch result
has yet been received. The existing consolidated native task-group request
must be refreshed only **once** when this new group is ready, not per commit.
