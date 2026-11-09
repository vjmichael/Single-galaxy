# PLAY-STATION-03 — real station-owned refinery roadmap

**Status: 03A native batch engine implemented and independently executed; 03B–03D not yet implemented or accepted.** Stacked on PLAY-STATION-02 PR #119; this is a separate follow-up to issue #116.

## Architectural requirement

The game must require a real owned and existing refinery facility, finite physical station ore and geological provenance, staffing and energy, then place resulting minerals in **station-owned** inventory. The ship cannot silently reload the ore and use its own `RefineOre` or `RefineMineral` operation. One tonne must remain exactly 1,000 kilograms after every conversion. `StationHangar` is the authoritative private bulk-ore location, not a second simulated copy.

Current limitations of original game code:

- `UnloadOre` transfers bulk ore tonnes from an actual docked original ship into `hangars_[{station,owner}]`. Hangars currently retain **only tonnes and capacity**, not ore-lot rock provenance, named minerals or staff/power.
- `Mine` for a geological asteroid transfers named phase concentrates directly to the **ship's** `ColonyShip::minerals_kg`; the same mass does **not** appear in bulk cargo. Reusing the resulting taxonomy to refine a separate station bulk-ore tonne without first implementing an actual transfer would **duplicate mineral mass**.
- `RefineOre` and `RefineMineral` currently consume the ship's cargo or onboard mineral stock. Neither is a genuine station service.

## 03A — bounded physical batch contract (this PR)

`include/galaxy/StationRefineryBatch.hpp` is a strictly deterministic C++ *component* using the **same** original `mineral_profile(AsteroidClass)` recipe rather than a guessed new table. A batch has a physical site, owner, original rock, nonzero verified `UnloadOre` receipt and remaining one-tonne allotment. A refinery instance has station/owner, existing equipment origin, two modules, assigned workers, finite power and a monotonic receipt. It consumes one tonne **from** a passed `StationHangar` stock while emitting exactly 1,000 kg of persistent-intended named station mineral stock, with a four-tick cooldown.

It rejects wrong owner/site, missing or corrupt lot provenance, nonexistent plant, zero workers, zero power, empty ore stock, repeated receipt, cooldown and full/corrupt outputs **without mutating original resources**. Output maps are built before source stock is debited to prevent partial mutation on allocation failure.

**Important boundary:** the batch function has no direct SQLite or `Simulation::Action` entry point. The presently provided C++ test instantiates a physical hangar and facility *as test inputs*; it does **not** prove those equipment modules, workers or geological lots came from actual committed gameplay. Do not display this step as complete station refining in Godot.

The exact local Linux Release test target `galaxy_play_station03a_refinery_ledger` has passed; GitHub Windows/Linux exact-head jobs are separate pending acceptance. Two test profiles: metallic ore = nickel-iron 860 kg, troilite 80 kg, olivine 30 kg, pyroxene 15 kg, other 15 kg; silicate test contains 400 kg plagioclase. After two batches total station minerals are exactly 2,000 kg with zero unrefined lot stock. Tests also cover output overflow, invalid negative inventory and denial atomicity.

## 03B — world authority and resource provenance (required)

1. Extend the original *authoritative* station ore ledger with physical lots tagged by source rock/class, physical source ship, actual dock/unload command receipt, station ID, owner and quantity. Implement both **bulk ore** and **already-separated geological minerals** as distinct resource paths—do not double-convert the same tonne.
2. Add host-authorized installation with real modules consumed from an owned ship; workforce transferred from existing NPC records and power supplied by installed station energy production. No free default refinery. Deny lost/no-ship/no-module/no-worker/power-loss paths.
3. Admit a named station refining command through the original `Simulation::submit` and `execute` path. Record finite resource transfers and finished mineral products in the same original `PersistentWorld` SQLite STEP journal. Extend canonical snapshot/readback, strict validation, world-mass accounting, actor permission and replay protection. Existing v122 saves must restore **without fabricated facilities or stock**.
4. Enforce time between batches and physical station storage limits. Ensure failure injection, crash/restart and SQLite tamper tests preserve mass or fail closed.

## 03C — actual station host and Godot

Only after 03B is verified, extend the station command pipe with `REFINE` and a separate authoritative `REFINERY` status frame; Godot may request but never execute production locally. The user must be physically inside the station at an existing refinery terminal. Show selected real ore lot, input mass, geological fractions, staff/power, ready time and tailings/byproducts; prevent creating a second refinery merely by reopening the UI.

## 03D — required full acceptance

Ship mines exactly one tonne in original space, returns and docks, unloads into the persistent station hangar, the station processes that **same** finite tonne, the named mineral inventory survives restart, and independent original SQLite reconciliation shows no mass created or lost. Verify no ore, stolen lots, destroyed vessel, absent facility, unstaffed facility, loss of power, failed/refused command, double execution and process crash on real Linux and Windows. Real installed Godot scene/camera/user interaction remains a distinct visual acceptance gate.

Related tasks: original physical ship/asteroid collision #114; visually fitted mining equipment and asteroid #117. Never assume a player may mine asteroids on foot.
