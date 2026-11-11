# Group 3 — Mining → Industry (execution register)

**Branch:** `sg/group3-mining-industry-20261111`, founded from independently hosted Windows/Linux VIS-01 source `b5d7057928d87d3cb1a05e07ff1d784a9425ad5f`. Do not merge the parked experimental PR #124 wholesale: its 04A source predates v125 swept physical collisions and SGDX native receipts. Reconcile semantics and preserve the accepted flight/world code.

**Non-negotiable:** Only the original Simulation/PersistentWorld can own rock ore, fitted modules, ship cargo, station hangar lots, refinery output and physical NPC/workforce capacity. Godot renders and requests commands only. No free drill, ore, replacement hull or silent save upgrade. All modules/refining require finite material and operative power/crew. All outcomes require exact original journal receipts, SQLite recovery, replay/tamper refusal and mass balance. Owner's physical pilot baseline is sufficient to advance engineering; do not repeat basic boarding tests as a development gate.

## Group 3A — Actual ship fitting and module manufacturing (active)

- Original v126 **genesis-only** mode allows one manufactured type-4 mining drill in a real hardpoint (2 physical tonnes per module). Existing v125 and older saves are **not** automatically upgraded.
- Actor must be an owned original colony ship, docked at an existing station with stable jump state and at least two crew. An actual `FabricateModule` command converts previously refined physical tonnes to an inventory module; `ConfigureMiningTool` atomically installs/removes that inventory module. Hardpoint power uses the existing trusted ship-status engine, not a UI toggle.
- Acceptance: genuine native Windows/Linux compile, original C++ material conservation, no-drill/no-mass denial, no duplicate module, unsupported slot, no ownership/crew, stale command/replay, SQLite reopen and signed world integrity.
- **Known game-content dependency:** the existing station test genesis contains no gifted drill, refinery, or mine-ready refined stock. This is intentional. Manufacturing a first drill requires real external physical supply or a separately authorized material-backed starter production/contract chain. Do not claim a new world is immediately mineable without that bootstrap.

## Group 3B — Asteroid survey

- Current `SpaceMiningContact` is a **30-metre proximity/collision readback**, not long-range sensor. Add an original C++ read-only bounded survey with same-cell 3D coordinates, mass/rock geology and ownership/provenance; return a target ID and bearing/range through the supervised pipe.
- Godot 8.5km render capability is **not** resource discovery. Display only original surveyed asteroids with sensible LOD; never create terrain minerals or mining targets locally.
- Acceptance: source-backed distant target, stale-session denial, exhausted/depleted omission, exact tick/journal join, no scan without real hardware/authorization.

## Group 3C — Ship-only finite extraction

- One authenticated seated native pilot + an operative powered drill may request `MINE` against the last verified asteroid ID. Original kernel validates same physical frame, effective hull-to-surface range, cargo capacity, remaining ore and exact `amount=1 tonne`.
- A successful original SQLite event subtracts exactly one tonne and adds exactly one tonne to that ship plus **one nonduplicating geological lot**. No hand mining, automatic yield, client-authored mineral stock or multi-tonne command. Reject power off, lost tool, docked ship, lost hull, unauthorized actor, replay and depletion.
- Acceptance: real original C++ ship/rock/lot mass balance, sensor/command pairing, durable shutdown and restart, matching Godot receipt.

## Group 3D — Physical cargo custody

- Reuse the **existing** original dock/undock, `UnloadOre` and station custody ledger: source ore is held on the ship until docking and only enters an existing station hangar on an authorized physically present owner action. Existing `REFHEAD/REFLOT/REFMIN/REFEND` protocol remains source of truth.
- Acceptance: exactly one tonne through original ship, station and SQLite, wrong station/ship/empty hold/foreign owner/duplicate unload refused, no instant teleport.

## Group 3E — Station refining and manufacturing

- Reuse `StationRefineryBatch`, original `RefineStationOre` and existing station terminal. Processing requires an actual installed/staffed/powered station facility, one source-tagged physical lot, energy consumption and output named mineral stock exactly 1,000 kg with existing recipe fractions and tailings.
- Connect finished minerals to original industry manufacturing and the 2-tonne first-drill supply chain through real inventory/work orders. Never silently reload the station's tonne aboard the ship to pretend refining was station-owned.
- Acceptance: original mined tonne -> dock -> unload -> station staff/power -> refine -> durable named output -> manufacture eligible goods, verify both Windows/Linux, interruption replay, mass conservation, source provenance and saved-world reopen.

**Status:** Group3A kernel integration underway. Group3B–E have dependent original code and test evidence but are not yet a complete player-controlled production vertical slice. No release, main-merge or physical R9700 claim.
