# PLAY-STATION-04 — visible original-space asteroid → ship-only mining roadmap

**Stage 04A status: real Linux original C++ contact and child-process telemetry tested; Godot parser and actual Windows scene play remain pending.** This task is the beginning of issue #117, stacked on draft PR #122 (persistent refinery terminal), #121 (SQLite refinery world), #120 (station mineral batch), #119 (station boarding).

## 04A — finite asteroid and native external visualization (implemented)

- Original `Simulation` contains the only authoritative asteroid IDs, 3D galactic positions, geological class and remaining ore. The read-only helper `include/galaxy/SpaceMiningContact.hpp` selects a nearby **real** asteroid within 30m in the owned ship's local frame, capped at 8,192 system entities scanned, and computes bounded 3D offsets without overflowing high-order galactic sector coordinates. The helper never produces mining events or changes material.
- The optional **explicit test-station fresh-world fixture** now contains one realistic metallic asteroid 25m from the initial station, 10m radius, density 900kg/m³, exactly 3,770 tonnes originally. The fixture has an existing finite player ship and a separate colony population required for original galactic coordinates. It does **not** add a rock to any previously saved SQLite world and does not give a new player a ship after losing the last hull.
- The *real C++ player host* emits `ROCK|1` or `ROCKNONE|1` **after** each original `SHIP|1` observation, including initial identification and journaled flight actions. Frames include host session, exact request, authoritative tick/generation, owned ship, rock ID, finite remaining ore, near-field X/Y/Z offset, physical radius and taxonomy, onboard cargo tonnes/capacity and a read-only mining tool status. This is a local observation, not a client-controlled asteroid spawn or mining command.
- `native_player_session.gd` accepts exactly one corresponding asteroid frame per previously verified `SHIP` tick. It rejects unsolicited, stale, malformed, cross-session, out-of-range and impossible docked-rock frames. Godot never writes original ore, coordinates or mining state.
- Godot adds a **V-key external visual view** while the original ship is actually in space, with a finite-resource asteroid mesh positioned from the received native relative offsets (not from a decorative fixed coordinate). Hidden ship cabin collision bodies are disabled when showing exterior space. The original status HUD reports real ore, cargo and mining equipment; no physical Godot asteroid collider is treated as authority (authoritative ship-asteroid collision #114 is still open).
- Ships currently have **no installed mining drill**. The helper only reports future module hardpoint ID 4 if a real installed powered tool exists. There is currently **no way to install this module through gameplay**. No client mining command is admitted at 04A. This is deliberate: merely seeing a rock cannot authorize a free resource harvest.

## Direct native evidence (executed)

Compiled the original C++ kernel-backed 04A header/test and actual `galaxy_desktop_player_host` on Linux, and verified exact Git blob hashes against the 04A branch. Read-only contact unit passed owner isolation, finite source rock/ore, high-order sector-boundary delta and original canonical snapshot immutability.

Then compiled a separate real original `PersistentWorld` SQLite test fixture containing a legitimate owned docked ship, player persona, colony population and finite geological asteroid. Started the actual C++ child host, received paired `POSE/SHIP/ROCKNONE`; sat in the cockpit, requested original `FLIGHT UND`, and observed authentic `SHIP/ROCK` with offset 23,500mm. Another original `FLIGHT FWD` moved it to 23,250mm relative to the player ship. The rock ore stayed 3,770 tonnes; mining tool remained unavailable. Closed/reopened the child and received the same original rock and stock from SQLite. Existing cockpit child-pipe regression was updated for the additional `ROCK/ROCKNONE` frames and passed locally.

Native tests cannot prove Godot 4.7.2 actually renders the mesh on the user's physical Windows GPU. A separate synthetic Godot parser smoke and a Windows/Linux exact-head CI workflow are added. Its precompiled Windows ZIP is **not** published until MSVC native tests, SQLite native pipe and real Godot parser checks pass.

## 2026-11-08 creator order: first finish 3D, defer ship modules

**This overrides the older 04B→04C dependency order below.** Do not design or expand mining-tool hardpoints, ship module crafting, slots, power, or fitting UI until the native 3D world actually works in Godot on Windows. Draft PR #124 is retained as an unaccepted **experimental proof-of-concept**, not a prerequisite to fixing the world. Native ship-only mining must remain unavailable on unfitted hulls; do not replace a proper module with free mining or hand tools.

**Active next task: FP3D-ENV-01 (issue #114, PR #125):** original high-order 3D ship/asteroid physical collision, bounded swept contact, durable outcome, and Windows/Linux tests. Next: render parity, camera/interior/exterior station transitions, ship control and real installed Godot playtest. Then revisit production module design and the actual one-tonne mining/return/refining gameplay. Existing 04A work is the clean world-first ancestor; do not import experimental 04B module code into the physics branch.

---

## Historical 04B plan — experimental and deferred until 3D acceptance

1. Introduce a **manufacturable, material-backed fitted mining module** occupying a valid ship rack, with finite consumed resources, engineering prerequisites and power/operative state. No automatic install on every hull; zero ability after all vessels are destroyed. Distinguish real mining tool from survey scanner and weapons.
2. Bind a cockpit mining action (e.g. `M`) to a selected original asteroid ID and a valid seated, undocked, existing owned ship with the real fitted powered module and cargo space. Original `Simulation::submit` and `PersistentWorld::step` must commit **one exact tonne** of transferred ore, never a local Godot reward.
3. Use original 3D distance, exact rock ore decrement, cargo custody and source-lot geology provenance; no mining on foot, while docked, out of range, using a foreign ship, or while the tool is offline. Never double-debit a geological ore tonne or produce both raw ore and separated concentrates.
4. Test the C++ original ore/cargo ledger, deliberate failure/rollback, replay, restart, stolen ship, failed tool, depleted rock and no-available-ship branch on actual Windows and Linux. Keep physical asteroid/ship collision issue #114 separate from source-range enforcement.

## 04C–04D — full player-visible one-tonne loop (not yet complete)

Visual mining effect driven by original executed receipt, return navigation, physical docking, `UnloadOre` into the real station hangar, station terminal `REFINE` one-tonne batch, actual 1,000kg station mineral inventory, restart and mass conservation. User-built refinery and module equipment must be created from finite workforce/materials rather than free test-fixture shortcuts. Then independently test the installed Windows Godot 4.7.2 game from station spawn → board → undock → fly → mine → dock → unload → refine.

## 2026-11-09 — PLAY-STATION-04C: reviewed station unloading bridge (source implemented; native acceptance open)

This is **not** a reversal of the creator's world-first order. Production mining
modules, crafting, fitting and the cockpit M-mining command remain deferred
until physically playable Windows 3D acceptance. PR #124 remains experimental.
No unowned ore, automatic drill or Godot-authored inventory was introduced.

The SGDX-supervised original **station** host now recognizes
`UNLOAD|session|request|expected_sqlite_generation|owned_ship_id`.
It accepts requests only from the authenticated station resident physically
within 1.2m of the original terminal; the selected ship must independently
exist, be owned, intact, available in the real station berth, docked there and
carry at least one original tonne. The host does **not** trust an amount from
Godot: it submits one `Action::UnloadOre` against the original owned vessel,
executes `PersistentWorld::step`, verifies the exact command journal event
and returns the existing source-owned `REFHEAD/REFLOT/REFMIN/REFEND` snapshot.
Result code **3** means one original tonne unloaded; **4** means a denied
world command; unchanged **1** means 1000kg refined and **2** denied refining.
The Godot parser does not confuse tonnes with kilograms, requires exact
session/request/tick/generation/lot count, and does not update inventory before
an original receipt. The terminal exposes one controlled unload action.
A stale, forged or empty ship cannot create stock.

A test-only `--04c-fixture` extends the existing independent original-world
03B test: construct legitimate materials, refinery and utility; execute an
actual one-tonne geological `Mine`; physically redock; retain the genuine
tagged ore on the docked owned hull. New
`galaxy_play_station04c_native_unload_pipe` then launches the **real C++
station host**, checks refused remote terminal use, invalid ship ID and stale
generation, walks a persisted person into range, performs exactly one tonne of
original unloading, verifies the tagged source lot, denies replay and an empty
ship, refines that same lot to exactly 1000kg named minerals, and verifies
`PRAGMA integrity_check`, original snapshot and restart continuity.
Godot's synthetic 03C/04C parser smoke checks distinct result types; synthetic
frames cannot substitute for native tests.

**Evidence:** implementation, CTest registration and the additional native
test have been committed to PR #125 with interim `[skip ci]`. The **single**
central SGDX03 task-group Windows/Linux/Godot gate on main includes the 04C
native test, but no 04C native run has completed on the updated source yet.
As of source review, the preceding original SGDX03 run
[42669612497](https://github.com/vjmichael/Single-galaxy/actions/runs/42669612497)
had passed request authentication but Linux and Windows workers were queued,
and that run was pinned to a **previous** PR head. It cannot accept the
new 04C changes. No installed Windows physical GPU playtest is claimed.
Full player mining remains unavailable because production equipment and
authoritative mining ingress remain deferred.

### Remaining 04C/04D playable loop gates

1. Get actual current-head native Linux/MSVC and Godot 4.7.2 results after
   task-group implementation review; fix demonstrated failures, preserving
   one consolidated gate, not one CI job per commit.
2. Complete Windows non-headless installed player flight/camera/ship-station
   collision acceptance and prove station/cockpit host handoffs and durable
   SQLite custody on a physical Windows machine.
3. Only after world acceptance, integrate manufactured fitted mining equipment,
   explicit native seated-pilot `M` action and verified one-tonne source debit,
   while keeping missing/disabled equipment unable to mine.
4. Run the complete live original-game `station -> board -> undock -> fly ->
   mine -> return -> dock -> unload -> refine -> inspect persistent minerals`
   playthrough, including crash/restart, resource conservation and no free hull.
