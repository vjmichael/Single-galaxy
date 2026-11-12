# S6-F — Consolidated planetary arrival, terrain, survival and recovery acceptance

**Status (2026-11-12): source implementation staged; consolidated native and installed release acceptance NOT PASSED.**

This ledger tracks the *original* Single-galaxy player-person and SQLite world.
No independent Godot motion, landing, resources, fake atmosphere or offline
survival simulation is permitted. All process and replay claims require original
host/journal evidence, not just a headless renderer screenshot.

## Implemented source path

| Workstream | Integrated source | Acceptance level |
| --- | --- | --- |
| S6-F1/F2/F3 original survey/landing foundation | Original celestial source, site/shuttle and terrain packet consumers | Prior source work; native/installed evidence must be reconciled |
| S6-F4 player-person transport | Registered PlayerPerson, authorized boarding/descent/disembark/return, persistent shuttle/source identity | Source and SQLite tests added previously; no installed gameplay gate passed |
| S6-F5-A/B horizontal terrain movement | Canonical geographic positions, source height, swept collision, region seams, immutable receipts | Source-implemented; seam tests staged |
| S6-F5-C vertical physics & environmental survival | Original mass-derived gravity, grounded/jump state, durable O2/CO2, pressure/known-air distinction, thermal exposure, injuries, suit and medical receipts | Source-implemented; real installed survival pending |
| **S6-F5-E terrain renderer** | Authenticated host emits a bounded 3x3 window of original 17x17 patches; immutable survey/geometry and numeric bounds; Godot merges source tiles into one non-colliding mesh and follows the signed-mm odometer through floating-origin rebases | Source integrated; optional headless Godot smoke registered but NOT executed |
| **S6-F5-E character** | Actual Godot player controller displays original XYZ, suit and injury; optional F1 third-person; native planet mode disables Godot capsule collision/raycast and does not call local physics | Source integrated; windowed visual/control acceptance NOT executed |
| **S6-F5-E background recovery** | Existing 100ms original-world journaled ticker independent of input; added multi-tick SQLite recover-copy and process-kill/restart acceptance fixtures; new host epoch rejects stale commands; initial reentry MUST be exact persisted state with no offline catch-up; Godot's 3s PFRAME heartbeat watchdog invalidates a stalled display and locks native movement pending recovery | Fixtures staged; subprocess run and OS/kernel restart verification outstanding |
| **S6-F5-F installed shuttle transfer** | Same desktop native executable gains `--planet-transfer` mode under the original SQLite OS writer lease, bounded original shuttle-site enumeration, explicit human BOARD/DESCEND/DISEMBARK/ASCEND/EXIT with native commits, source-validated TSITE/TFRAME/TLAND/TRETURN; Godot transfer panel and role handoff; original-hatch E reboard with committed PLEAVE | Source implemented; exact-head compilation, transport subprocess test, Godot panel, SGDX supervision and installed human game **NOT RUN** |
| **S6-F6 continuous/recovery** | Existing native 100ms journaled original surface/shuttle clock; new lease-held read-only `--planet-recover` role decision from original person/site/shuttle receipts, exact-save Godot startup preflight, at most one auto-crash reattachment after SGDX PID termination, no offline ticks, ambiguous-save/orphan-WAL refusal, full uint64 planet identity across Godot terrain/transport | **Source implemented; exact-head native/Godot, real SGDX kernel restart and installed human acceptance NOT RUN** |

## New tests, by their exact registration names

1. `galaxy_stage6_native_surface_movement`: extended with 3x3 host atlas identity/bounds/denied-owner and consecutive native-tick SQLite copy checks.
2. `galaxy_stage6f5e_planet_host_recovery`: creates a genuinely landed original person, starts the original `galaxy_desktop_player_host --planet`, observes autonomous frames, kills the host, inspects original SQLite, restarts, rejects an old-session movement and applies one fresh native command. **Not yet run.**
3. `galaxy_stage6f5e_godot_tile_window` (when a Godot 4 binary is present): loads the actual renderer and checks nine tiles, floating-origin rebase, wrong-session denial, and mixed-body denial. This is a **synthetic renderer input fixture**, not evidence of native ownership. **Not yet run.**
4. `galaxy_stage6f5f_original_shuttle_roundtrip` exercises a real source-built orbital site, committed person boarding, native descent, forced process death, fresh epoch denial, disembark, source-hatch reboard, ascent, exit and original SQLite mass conservation. **Authored, NOT RUN.**
5. `galaxy_stage6f5f_godot_shuttle_source` checks strict Godot source-shuttle packets and operator controls with synthetic packets. **Authored, NOT RUN; not native gameplay evidence.**
6. `galaxy_stage6f6_original_role_reconciliation`: five true original SQLite checkpoints (ship/boarded/in-flight/landed-shuttle/surface), immutable read-only probe, live writer lock refusal, 16 unsolicited original world ticks and no offline catch-up. **Authored, NOT RUN.**
7. `galaxy_stage6f6_godot_recovery_source`: strict source-role/identity packet/parser negative fixtures. **Authored, NOT RUN.**
8. `galaxy_stage6f6_godot_unsigned_source_id`: full C++ unsigned body ID no rounding in recovery and shuttle; F5-E Godot tile test also now covers 20-digit planet identity. **Authored, NOT RUN.**
9. Existing S6-F native-math, original landing and Godot/SGDX tests retain their separate admission criteria.

## Required consolidated verification sequence

**A. Native unit and transaction acceptance (pending):**
- Compile the exact staged SHA on native Linux and MSVC Windows; run S6-F original planet/landing/player-custody/movement/math/atmosphere/body-gravity/vertical tests.
- Run the staged F5-E, F5-F and F6 native/host/Godot acceptance fixtures and inspect their real stdout/stderr, exit codes, exact source SHA, and original SQLite receipts.
- Challenge interrupted commits, duplicated commands, wrong owner/body/site, mixed-generation/stale tile content, ocean/unknown survey, invalid bounds and source-mass/air tampering.

**B. Full real-process recovery (pending):**
- Start the actual SGDX-supervised planet child, suspend Godot input, verify original ticker progression, then crash/restart Godot, original C++ worker and guardian separately.
- Confirm OS-only writer custody, no orphan writer, no double SQLite owner, durable survival/movement state, new epoch and original source pose, no ghost tick replay, and operator-visible uncertainty on unreconciled state.
- Challenge the new `--planet-recover` process against a real active guardian lease, suspended/stalled child, preflight timeout, saved in-flight shuttle and failed native action before ACK. The new writer may start only once old SGDX and original OS custody have actually terminated. Source: [F6 ledger](engineering/STAGE06F6_CONTINUOUS_SIMULATION_RECOVERY_20261112.md).
- Run the scenario on the actual Windows install and Linux host. A Python direct-host fixture does not satisfy kernel custody.

**C. Installed planet and character acceptance (pending):**
- Use an actual authorized original arrival, not a preseeded/unlanded fixture, in the installed Godot application.
- Traverse across multiple 64m streamed tiles, source-cell seams, longitude wrap and high latitude; verify terrain/character match original survey, visual rebases, original contact and injury, foreground controls and 100ms idle survival ticks.
- Exercise F1 camera, jump, suit charging/equip, life support, original medical recovery and re-embarkation with durable receipts.
- Test missing/unknown atmosphere WITHOUT fabricated hypoxia, water/ocean and hazardous regions without fake walkable floor, and valid refusal for missing surveys.
- Verify package contains the native host, SGDX supervisor, Godot scripts, source assets and all required SQLite migration paths. Run installed Windows human and Radeon/GPU sustained playtest.
- **S6-F5-F source addition:** real live-cockpit F9 can hand off its *actual* original SQLite world to a source-verified shuttle site selector and the existing native transport actions; a completed DISEMBARK opens the genuine surface host, and hatch-only E reboard later opens the native ascent/EXIT path. **This is only staged source, not an installed accepted flow.** F6 now source-stages read-only startup role reconciliation for recognized original saves and one bounded crash retry; genuine native guardian/reboot evidence remains open. See [F5-F ledger](engineering/STAGE06F5F_INSTALLED_SHUTTLE_TRANSIT_20261112.md).

**D. Release gate (blocked):**
- No new PR, CI acceptance, merge, release or installed-app acceptance claimed on this branch. F5-F/F6 source implementation does not close F5-E or F6 native/installed acceptance.
- Mark S6-F consolidated acceptance complete only after A+B+C pass on a single exact source head, with failure evidence retained. Otherwise source work is provisional, and Stage 6 G remains separate.

## Reproduction / branch boundary

Source branch: `sg/stage6f5e-installed-terrain-recovery-20261112`, descending from unreferenced EvEmu 83 commit `618f7ea3cbe280496df71a4e28409711f7e0eb07`. That predecessor contained 23 commits beyond the older `sg/stage6f5c-native-gravity-survival-20261112` branch tip, so avoid silently resetting to that older ref. Preserve the original `main` and perform no merge without explicit authorization.
