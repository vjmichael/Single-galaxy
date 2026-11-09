# PLAY-STATION-04B: installed ship mining and exact one-tonne extraction
Status: experimental draft **deferred by creator** until the native/Godot 3D world works; acceptance remains open.

This is a stacked 04A follow-on, using the same original C++ simulation, SQLite
journal and geological custody lots. Version 124 is opt-in only during new-world
genesis after version 123 station provenance. Existing save semantics remain intact.

The real colony ship starts with no installed mining drill. The explicit 04B
test genesis contains exactly TWO tonnes of finite refined material: a genuine
FabricateModule action converts them to one two-tonne module, and the original
ConfigureMiningTool action installs that module in a free fitted hardpoint at
a genuine docked berth with a crew of at least two. Removing the tool returns
one physical module. The world mass computation includes the installed module.

Original hardpoint ID 4 is a drill. Its requested power may be disabled through
SetHardpointPower, and the original ship status engine determines effective
power. The kernel checks in-space custody, existing owner/ship, local 3D frame,
range to physical asteroid plus radius, ore remaining and real cargo capacity.
Version 124 accepts ONLY Action::Mine(amount=1) per command; StartMining is
disabled in this gated mode. The one real tonne moves from the physical source
rock to that original ship and creates a non-duplicating geological origin lot.

Godot only emits native intents. The seated original player uses G to fabricate,
F to install, U to remove, K to toggle power, M to mine the last verified rock
contact. Native child checks seat, owner and selected original rock, submits
the immutable command, advances the original world and checks its exact event,
then emits original SHIP and ROCK receipts. The UI does not write SQLite.

The separately packaged Windows playtest uses RUN_MINING_04B_WINDOWS.cmd, a
compiled native test-only genesis executable, an explicit save path and existing
Godot 4.7.2. The seed refuses to overwrite a saved world; ore, module stock and
losses persist. A physical Windows playtest must still verify installed UI,
control feel, native worker recovery and any false success indications.

Acceptance requires real Windows and Linux native builds, C++ module/mass
invariants, real original child-stdio extraction and restart evidence, Godot
runtime parsing, and eventual installed physical Windows verification. Source
checks and synthetic Godot frames are separate evidence, not completion.

## New priority: World first, modules later

Per creator decision (2026-11-08), stop working on ship module fabrication, slot fitting, power and variant design until the original 3D world and collision/visual play loop work on Windows. This branch's fitted mining tool is an **unaccepted experimental prototype**, not the approved final module architecture. Continue with FP3D-ENV-01 (issue #114) and Godot 3D world acceptance on a **separate branch from 04A**, without relying on this module work. Keep draft PR #124; revisit this source only after 3D acceptance. Details: `SHIP_MODULES_POST_3D_WORLD_ROADMAP.md`.
