# S6-F6 — Original-world continuous planetary simulation and crash/restart reconciliation

**2026-11-12. Development branch:** `sg/stage6f5e-installed-terrain-recovery-20261112`.
**Status: implementation source staged; native executable, CTest, Godot runtime, SGDX guardian, installed Linux/Windows and physical Radeon acceptance NOT RUN.** No merge/release authorization.

## Original custody and clock (no parallel world)

The `--planet` and `--planet-transfer` modes of the existing `galaxy_desktop_player_host` already hold the **same original** OS `NativeWriterLease` / `PersistentWorld` and advance one canonical `world.step()` about every 100ms. These are journaled game steps, independent of Godot input cadence or camera focus, and their independently committed PFRAME/TFRAME attestations are bounded and continuously streamed. The writer serializes human actions and the native clock with its own mutex. An interrupted process stops this clock; **wall-clock downtime does not fabricate physics, injury, shuttle progress, manufacturing stock or NPC outcomes**. Original transport can resume from remaining actual persisted flight ticks once a new authorized host owns the saved world. PFRAME and TFRAME require fresh session epochs.

### F6-A — Native source-owned recovery preflight

- New pure `include/galaxy/Stage6RecoveryDecision.hpp` derives exactly one of **Station, Ship, Shuttle, Surface** from the original `PlayerPerson` and authoritative owner, home vessel, physical station, body, site, pilot, shuttle flight phase, current flight/descent/ascent receipts and original location identity.
- `galaxy_desktop_player_host SQLITE_PATH --planet-recover` is a **short-lived source readback** inside the existing native binary. It requires an existing regular SQLite file and acquires BOTH original OS writer locks *non-blockingly* before reopening the original saved world. `RFRAME|1|ROLE|STATE|SHIP|BODY|LAT|LON|SITE|SHUTTLE_PHASE|TICK|GEN` is emitted only when authoritative custody is consistent. A failed lease, absent player, forged/incompatible custody, unreadable saved world or unsupported person state fails closed.
- This mode does **not** request `world.step()`, guess a person from a camera, import material, generate a planet, or reissue any old action. Lock sidecars may be created or retained by the existing OS lease; the original snapshot/generation is not intentionally mutated. The post-probe writer independently reopens and revalidates original custody.

### F6-B — Installed Godot role recovery

- `native_planet_recovery_probe.gd` runs that native mode through **SGDX** and accepts a single strict `RFRAME`. It does not admit a replacement original-world writer merely because stdout contains a packet: the **supervised OS guardian must actually exit** first. A 5-second probe timeout attempts to terminate its custodian, retains native movement lockout and never starts a concurrent writer.
- Existing installed startup first reads a **UI-only, user-selected SQLite path hint** (`native_saved_world_choice.gd`). A valid last selected path is reverified by the original native preflight, never trusted as authority. If no hint exists, startup examines the two recognized original station/legacy-authoritative SQLite paths. Two ambiguous saves require explicit human selection; missing or corrupt selected paths, corrupt path hints, and missing main SQLite with orphan `-wal/-shm/-journal` block genesis. The pointer is cleared only after an explicitly confirmed successful New Game archive. Only a truly absent prior world can take the existing first-station genesis path.
- Recovered **AtStation** reconnects the native station role with the **same actual saved path**. **AboardOwnShip** reconnects the original pilot; **InPlanetaryShuttle/PlanetaryTransit** reconnect original shuttle transport; **AtPlanetarySurface** reconnects the source-owned continuous planetary terrain/survival host. The native actors receive no client-proposed pose or forged previous receipt.
- Any *previously verified* unexpected surface or shuttle process exit triggers at most **one** automatic OS-PID-fenced preflight/re-attachment from the original SQLite source. An uncertain second failure blocks further auto-retries and asks for an explicit F9 retry. Deliberate successful TLAND, TRETURN and PLEAVE transitions suppress crash recovery and retain the one-writer role handoff. Neither unacknowledged human transport actions nor native movement commands are replayed automatically.

### F6-C — Exact original celestial identity

Planet body IDs are **C++ unsigned 64-bit identifiers**, not signed Godot integers or JSON floating-point values. Added `native_source_id.gd` to verify exact, canonical 1–20 digit uint64 decimal strings through the maximum `18446744073709551615`. Preserved strings through source preflight, TSITE/TFRAME/TLAND, PFRAME/PLEAVE, Godot player status, and 3×3 atlas matching. Reject zero for a planet, overflow, changed body, negative, leading-zero and mixed-original sidecar references. No body ID is rounded into a new planet.

## Authored source regressions — NOT EXECUTED

1. `galaxy_stage6f6_original_role_reconciliation` uses the original-world native fixture to generate authentic **orbit, boarded, in-flight, completed landing inside shuttle and disembarked surface** checkpoints. For each, assert native `--planet-recover` role, immutable tick/generation/world hash, original home ship and site.
2. Start an actual original-world shuttle writer, confirm second-probe **OS lock rejection**, then accumulate at least 16 unsolicited durable native ticks with no user commands, forcibly kill the process, reopen SQLite and establish no subsequent offline `world.step()`. Fresh probe must report exactly the recovered generation, never an invented elapsed-time catch-up.
3. Recover an original in-flight voyage after real time elapsed while no child was running. The first live transport frame must equal the last original persisted tick/generation, and only a newly admitted native writer may resume the remaining three-tick flight.
4. `galaxy_stage6f6_godot_recovery_source` checks bounded `RFRAME` identity/role rejection in the Godot script runtime. `galaxy_stage6f6_godot_unsigned_source_id` checks overflow and full-width uint64 body preservation. `galaxy_stage6f6_godot_selected_world_hint` checks preference persistence, removal and corrupt-hint denial. Prior `galaxy_stage6f5e_godot_tile_window` now stages a high-ID source atlas.
5. Existing F4, F5-C, F5-E, F5-F, WO3/4/5, dock, original source-consistency and SQLite-recovery tests **must also be rerun on the consolidated new SHA**.

No test pass is claimed by its source registration.

## Important remaining acceptance and risks

- **Real native compilation and execution remain open.** No local copy of the full repository or actual Godot executable was available in the current worker runtime. No exact-head C++/Godot compiler, CTest, installed app or Windows/Linux test was executed. Repair demonstrated errors when those genuine results are available.
- **OS process custody:** Independently kill and restart the SGDX supervisor, child native writer, Godot and the actual OS session on Windows and Linux. Verify Windows Job Object/pid-tree cleanup, live lease conflicts, no orphan/overlapping writers, stale epoch denial, no uncertain action replay and correct post-restart journal readback. A subprocess test of the child alone does not prove this.
- **Human installed journey:** On actual packaged Windows/Radeon, verify original ship/registered planet, physical shuttle landing, source 3×3 terrain, correct survival/suit/O2/gas return, near-hatch reboarding, ascent and return to ship. Examine pacing under active 100ms native commits and prolonged idle. Physical play and multi-hour endurance are not verified.
- **Migration/corruption:** v86/v130 historical SQLite corpus, in-progress WAL crash variants, body/source migrations, journal tampering, rollback and exact old save preservation are not independently accepted.
- **Scope:** The installed startup restores the last explicitly selected original save pathname as a UI hint, or discovers the two known desktop filenames when no hint exists; ambiguous/corrupted hints and unsupported originals require human F9 choice. Unsupported player custody (detained/rescued/stranded), multiple ambiguous saves and invalid historical sources intentionally refuse planetary role. S6-F5-D NPC paths, S6-D/E environmental cargo, S6-G scale/migration acceptance and longer-range user interfaces remain open.
- **Capacity:** Native 10Hz canonical SQLite commits are functional design intent, not a proven long-duration sustainable disk/CPU/latency configuration. Sustained multi-body load and strategic cold/detailed simulation need project-wide S6-G acceptance.

**Gate:** S6-F6 source development can be parked provisionally. **Not accepted/closed** until exact-head native execution, protected guardian/cold-restart tests and real installed-game acceptance pass. **Next: S6-G — Stage 6 integrated migration, scale and end-to-end acceptance.**
