# Proposed follow-on gates (not implemented)

- L1: Re-run all 46 checks on native Windows with a pinned SQLite amalgamation; verify Windows process relaunch, WAL recovery, file locking.
- L2: Bounded checkpoints and compaction, including durable depleted-ID tombstones and bounded event records.
- L3: Abrupt process kills across commit boundaries, disk-write failures, and SQLite `integrity_check` in recovery workflows.
- L4: Background tick scheduler with cancel/pause/continue; make mining cycles reflect physical modules, energy/cooldown and moving targets.
- L5: Collision-free mining target discovery, cargo offloading, ore refining, NPC actor and market.
- L6: Quantitative scale benchmark with 100–10,000 asteroids and multiple mining drones and dynamic entities.

These are tasks, not claims of completion. Historic EVEmu/Crucible research and client analysis are separate tracks.
