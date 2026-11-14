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
Double-click the ZIP root's `START_SINGLE_GALAXY_WINDOWS.cmd` (which
delegates to `desktop_godot/RUN_STATION_PLAYTEST_WINDOWS.cmd`). The launcher
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

### 2026-11-09 native Windows PowerShell 5.1 packaging repair

In real consolidated [Actions 42715286430](https://github.com/vjmichael/Single-galaxy/actions/runs/42715286430),
**Linux C++ 19/19 + actual Godot 4.7.2 scene/smokes PASSED**, and the
**Windows MSVC native/SQLite/private MDMP 19/19 PASSED**. The actual source
Godot Windows parser/scene tests passed as well. The ZIP itself was
constructed and re-extracted: `packaging-result.json` reported
`BUNDLED_GODOT_AND_NATIVE_WINDOWS_PE64_VERIFIED`, and the bundled engine
completed **all six** headless parser/scene smokes using modern PowerShell
inside the extracted image.

The security negative-test harness then launched the same
`Launch-PrebuiltStation.ps1` through **real Windows PowerShell 5.1** to
prove that a deliberately mismatched Godot binary SHA was rejected. The
actual captured `tampered-launch-stderr.log` and
`tampered-launch-stdout.log` showed the **real failure**:
`The term 'Get-FileHash' is not recognized as the name of a cmdlet`.
Thus the test correctly refused to give credit for a nonzero exit from
an unrelated PowerShell module error. No positive verified ZIP was uploaded
and automated non-headless screenshots were not executed.

**Repair:** the player's portable launcher no longer depends on an
installed PowerShell `Get-FileHash` cmdlet. It computes the exact SHA-256
with Windows' existing `System.Security.Cryptography.SHA256` and
`System.IO.File` APIs, closing both cryptographic and file handles
deterministically. Original engine, native-host and operator screenshot
identity checks retain SHA-256 strict equality; there is **no** bypass
and no network dependency. The consolidated packaged gate now executes
a **clean positive** headless smoke under actual
`WindowsPowerShell\v1.0\powershell.exe` and independently verifies its
`BUNDLED-VERIFIED` receipt before altering the extracted manifest;
only then does it require a specific hash-mismatch refusal with
`result=BLOCKED`, restore the original hash and verify source integrity.

**Status:** this Windows 5.1 compatibility repair and positive/negative
acceptance contracts are committed to draft PR #125 but require a fresh
single end-of-task-group Windows/Linux/Godot/ZIP/windowed rerun.
Physical creator Windows GPU camera quality, asteroid contact, docking
and saved-world continuity still require real on-PC operator evidence.

### 2026-11-09 actual Windows 5.1 repair refinement

The exact Windows artifact of [run 42715286430](https://github.com/vjmichael/Single-galaxy/actions/runs/42715286430)
contains both real `tampered-launch-stdout.log` and
`tampered-launch-stderr.log`. The 5.1 child had inherited a restricted
`PSModulePath` from the Actions PowerShell 7 parent and could not
resolve `Get-FileHash`. The unrelated failure was *correctly not*
interpreted as hash-mismatch protection; the original bundle was restored
and the job was rejected.

The portable repair computes SHA-256 directly with the built-in .NET
cryptography API and closes the streams. For Windows PowerShell 5.1
(`PSEdition=Desktop`), it also restores the system-owned
`System32\WindowsPowerShell\v1.0\Modules` search path so the default
Windows `ConvertTo-Json` and `Compress-Archive` evidence facilities
remain available even when launched under a mixed pwsh 7/5.1 environment.
The real 5.1 test **first runs the clean extracted game**, requires
a source-verified `BUNDLED-VERIFIED` receipt and full six-suite native
Godot parser pass, then tampers the engine hash and requires an exact
SHA-256 mismatch refusal and `BLOCKED` receipt. This is still a real
pending native gate, not a claim that the new repair passed.

## FP3D-WIN02 PS5 and paired-engine acceptance (November 9, 2026)

The latest actual Windows task-group gate
[42717276991](https://github.com/vjmichael/Single-galaxy/actions/runs/42717276991)
passed all 19 MSVC native CTests including private MDMP/SQLite, the native
Godot 4.7.2 scene, the entire Linux native/Godot gate, and the actual
self-contained ZIP PE64/engine/source/license check plus its SIX installed
Godot headless smoke suites under PowerShell 7. The Windows PowerShell
**5.1** positive launcher test failed at smoke #1 despite a normal-looking
Godot editor-load transcript and no GDScript parser error. The prior test
had started Godot's GUI-subsystem `win64.exe` directly as a command-line
test child, a different process/exit-code path than an explicitly console
hosted engine. Its failure is NOT a passing negative tamper test.

**Source repair pending next native gate:** ship both authentic executables
from the SAME official Godot 4.7.2 ZIP, which the group checks against its
published SHA-256 before unpacking: GUI `Godot_v4.7.2-stable_win64.exe`
for player window and companion
`Godot_v4.7.2-stable_win64_console.exe` for all headless/CLI smokes.
Verify each PE32+ x64 and independent SHA-256 manifest
(`GODOT_RUNTIME_SHA256.txt` and `GODOT_CONSOLE_SHA256.txt`) on original
archive, extracted package and the player's actual launcher. The console
wrapper supervises the real GUI engine and returns a final child exit code.
**Never** ignore nonzero exit codes, parser errors or missing screenshot
frames. The actual Windows PowerShell 5.1 positive test MUST pass before
corrupting only the GUI digest for a negative test; that test MUST fail for
the exact reported hash mismatch, create a blocked receipt, and restore
the original manifest. No published download until genuine Windows tests
pass.

The following four automatic windowed frames still require execution:
docked cabin, exterior flight, stopped original asteroid impact, restarted
SQLite world. A hosted runner's windowed screenshot is not the creator's
physical Windows GPU/human camera acceptance. PR #125 remains DRAFT and
manufactured ship mining equipment remains deferred.

## Actual Windows PowerShell 5.1 diagnosis — run 42720754880

[Exact-head native run 42720754880](https://github.com/vjmichael/Single-galaxy/actions/runs/42720754880)
passed all 19 Windows MSVC native tests including SQLite, SGDX and real
second-chance private MDMP, native Godot 4.7.2 and the entire Linux native
and engine group. The actual ZIP passed x64 PE/source/license/GUI+console
sha256 validation and SIX installed headless Godot scene tests under
PowerShell 7. The genuine Windows PowerShell 5.1 positive test still
**failed**, and its enhanced logs proved the precise failure: the
correct authentic console wrapper emitted no script errors, the process
object reported `HasExited=True`, but `ExitCode` was blank/null when read
after `Wait-Process -Id`. This was a bad process-observation contract,
not an observed failure of the original 3D simulation or a valid negative
engine tamper denial. The group correctly blocked download and skipped
the four-screen render.

**New repair pending native evidence:** use `$p.WaitForExit(90000)` and
`$p.WaitForExit()` on the **same** typed .NET process returned from
`Start-Process -PassThru`, not a separate PID-only `Wait-Process`
invocation. Refresh it, require `HasExited`, an explicitly NON-NULL
numeric `ExitCode`, and no script errors in either output stream.
Preserve bounded timeout and fail closed on missing/negative exit or
untrustworthy Godot output. The positive Windows PowerShell 5.1
headless tests and negative hash-mismatch denial **must both pass** in
the next consolidated exact-head task-group gate; only then can we
assert a self-contained Windows playtest ZIP is distribution-verified.

Four genuine non-headless rendered frames (dock, exterior, collision,
reopened SQLite world) and the creator's own physical-PC GPU judgment
remain separate release criteria. Keep PR #125 in draft; do not unlock
manufactured mining equipment.

## Actual WinPS5 process handle repair — consolidated run 42721988390

The [next exact-head run](https://github.com/vjmichael/Single-galaxy/actions/runs/42721988390)
again verified genuine Windows native C++ compilation and real Windows Godot
4.7.2; 18/19 native CTests passed, while the *source-only*
`galaxy_fp3d_win01_source_contract` failed because it still matched a
previous `$exited=$p.HasExited` literal after the repaired implementation
changed to an explicit Boolean cast. That source-contract string was updated
without relaxing any runtime acceptance.

The unmodified Windows PowerShell 5.1 package test still failed with an
unavailable `ExitCode` even after `$p.WaitForExit()` on the PowerShell
`Start-Process -PassThru` wrapper. No script error was observed; CLI
wrapper SHA-256 and standard PowerShell 7 smoke were valid. This is now
treated as a proven **PowerShell Process-wrapper result-observation
defect**, not an authorized reason to ignore an absent exit code.

The newest source repair avoids PowerShell's `Start-Process` wrapper
altogether for CLI smokes: create a fresh
`System.Diagnostics.ProcessStartInfo` / `Process`, explicitly set
`UseShellExecute=false`, redirect BOTH output streams, drain them
asynchronously to prevent deadlocks, enforce `WaitForExit(90000)`, then
read `Process.ExitCode` directly as a required numeric value.
Nonzero, missing status or Godot parser errors still block the player.
Actual on-screen playback continues to use the GUI executable.
The original official Godot console companion, separate checksums and
negative engine-tamper test remain mandatory.

**Pending evidence:** an exact-head consolidated native Windows/Linux
gate must prove the .NET direct-process path under *actual Windows
PowerShell 5.1*, a clean positive package launch, exact corrupt-engine
denial/restore, and four native-joined non-headless frames. Only then
issue the Windows ZIP for creator-run physical-GPU acceptance. PR draft
and equipment deferral remain unchanged.
