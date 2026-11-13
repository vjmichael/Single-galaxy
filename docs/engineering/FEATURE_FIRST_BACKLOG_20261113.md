# EvEmu feature-first backlog — November 13, 2026

**Priority:** Finish original gameplay/simulation functionality and backed-by-source acceptance before any owner-PC work. Explicitly defer physical Windows SCM, installed gameplay, Radeon retests, package/release and operator-run PC validation until the source backlog is addressed. Hosted native acceptance and GitHub-only development continue.

## Verified baseline
- Exact source SHA `342c81cbc994e9126e0629f2b2ebc3c6323c35c2`: original S6-G native Windows/Linux 46/46 required tests, process-custody 3/3 per OS, independently successful delegated Linux cgroup and hosted Windows SCM/Job, and four independent SQLite-world restart/recovery. The hosted Windows receipt is **not** physical-PC evidence.
- D7 geological freight, E6 staffed ecological production, F surface/NPC movement and S6-G kernel native regression closed against this accepted *baseline*; previous code remains unchanged except specific feature tests below.

## Implementation order — no local PC gates
1. **S6-H1 (this source change)**: 4 physically distinct original star systems, 4 actual occupied cells, 1 shared original SQLite authority. Each of 256 committed ticks schedules 4 real read-only OS workers; durable winner rotation, 7 supervisor restarts, 7 injected rollbacks, 4 original SQLite recovered copies, original ship/NPC/entity IDs and ore mass, no invented copies. Source-only feature simulation, not interstellar flight or four concurrent mutable writers.
2. **S6-H2 (source implemented; exact-head native acceptance pending)**: Genuinely journaled 250-mm physical sector crossing plus separately authorized cross-star gate arrival; each drives the same atomic post-movement worker lease CAS, empty-source retirement, crash-mid-handoff rollback and restart/idempotent recovery. Original ship, true galactic coordinate and SQLite immutable transfer remain sole authority. This is not new instant star travel.
3. **S6-H3**: Production-scale concurrent native worker throughput and bounded scheduler admission across a growing, one-original-world fleet; observe and repair actual latency/backpressure, no detached descendants, and durable replay denial. Four independent worlds alone do not count.
4. **S6-H4**: Historical v86–v135 original SQLite corpus and WAL/revision recovery if actual incompatible saves remain; preserve exact citizen, resource, medical/legal address and source audit identity without regenerating starting worlds.
5. **Stage 7 — population/society**: Continue individual NPCs, households, delegated labor, independent society, economic and legal behavior on existing authoritative game records; no fabricated NPC resources or consequences. Defer networking and release until supported gameplay exists.

## Native H1 scope and limits
- `galaxy_native_galactic_supervisor_multisystem_256` is a **new mandatory Stage 6 consolidated native test**, never a fixture-only substitute. It runs against one source SQLite database and a real executable OS worker per cell.
- Real separate system IDs 1–4 with immutable galactic origins; founder ship and finite life-support in system 1; other owner ships purchased/docked at original system-specific stations and finite original asteroids. Only four original occupied leased cells have selection authority.
- Existing S6-G 1,024-tick four-cell test, physical sources and one-writer fencing remain unchanged. The new test does **not** claim actual travel between systems, remote multiplayer, fully parallel mutable physics, physical Windows, or installed GPU proof.
- Initial development-container execution with the existing accepted native kernel passed the 256+256 tick pair and all rollback/recovery/identity checks; this is **not** an independent Windows/Linux exact-head acceptance receipt. Wait for original CI at the new source SHA before marking cross-platform H1 native complete.

## Governance
No merge/release/installation. Keep exact-head native receipts and inspect original failures before source fixes. Functional backlog implementation has precedence over routine hosted CI and device revalidation.

## S6-H2 implementation checkpoint (2026-11-13)
- `include/galaxy/SeamlessShipCellHandoff.hpp` — source-backed, immutable cross-cell worker handoff receipt with single-SQLite atomic CAS and failure injection; only original snapshot migration can authorize a move.
- `NativeMultiCellSupervisor::reconcile_ship_handoff` — bounded host API; no unapproved worker or capacity overflow.
- `tests/Stage06H2PhysicalShipHandoffTests.cpp` and registered mandatory `galaxy_stage06h2_original_cell_handoff` CTest — real 250-mm galactic sector crossing plus original gate arrival, crash-mid-handoff, stale/forged/replayed worker authority, recover copy, no resource/entity creation.
- Local GCC/Clang target passed against the baseline original kernel; new exact SHA Windows/Linux native results remain open. Proceed to H3 multi-cell fleet throughput and H4 old SQLite corpus before any physical PC testing.

## S6-H3 source implementation — bounded shared-world fleet scheduling
- Corrected a host admission gap: overlapping `run_next_tick`, `submit_original_command`, `resume_pending_tick`, worker registration, epoch takeover or H2 lease handoff on one supervisor now fail promptly with `original shared-world supervisor busy; retry later`, rather than racing the same mutable PersistentWorld / lease map / failure records or building an unbounded queue. An exception releases admission; no silent retry creates a second world writer.
- Bounded native preflights are now explicitly tunable from 1–4 concurrent lanes (never more than four leased physical cells). Each wave drains its OS subprocesses before further jobs launch; original one-writer tick selection/fairness and immutable SQLite claims still apply. Per-tick host metrics report preflight waves, eligible/failing cells, exact source generation, committed tick and measured elapsed ms; failed ticks clear prior success metrics.
- `galaxy_stage06h3_fleet_256` and extended required `galaxy_stage06h3_fleet_1024`: 64 original colony ships with 512 genuinely instantiated named NPCs and finite startup supplies in four original occupied physical cells of ONE SQLite world, 2 preflight lanes, native worker program launches, 256/1024 globally committed ticks, supervisor destruction/restart, original write failpoints, same-ID pending tick recovery, SQLite recover-copy verification every 64 ticks, immutable IDs, cargo/ore/entity persistence and fair cell scheduling. The test deliberately rejects stale dispatch replay and concurrent writes; it is NOT four independent mutable galaxies.
- Local Linux GCC native kernel test at the implementation source (pre-commit) passed 64 ships / 512 NPCs / 256 ticks and 1024 ticks, with 7/28 rollback-replays and 4/16 independent original SQLite recovery copies respectively. The 1024-tick run measured p50 32.9 ms, p95 42.7 ms and max 90.1 ms per original tick in this development container, **not** a benchmark for the physical PC or a service-level guarantee. Original Windows/Linux exact-HEAD acceptance at the eventual commit remains separate.
- **Not implemented by H3:** independent parallel mutable physics writers, LAN/multiplayer networking, interstellar propulsion without gates, unlimited worker count or autonomous cross-galaxy simulation. H4 historical save migration and Stage 7 societies remain next feature groups. No owner-PC, installer, Radeon, production deployment or release gates were run.


## S6-H4 historical native SQLite / WAL implementation (2026-11-13)

- Genuine original SQLite WAL hot-backup with committed-only inclusion, injected native snapshot rollback, preserved person/ship/ore/material state, corrupted-checksum fail-closed refusal and exclusive recovery destination locally passes GCC 14.2 and Clang 17.
- Pinned original v85 producer SHA, test blob, binary and SQLite checksum are mandatory for new historical WAL case, run in consolidated native CI. No synthetic historical version headers or legal addresses allowed.
- Version v85 has an original native source producer, but v86 is only authenticated in-memory upgrade; v86–v135 saved original producer corpus remains missing. New inventory emits that gap. H4 remains partially open until genuinely authenticated older source snapshots are recovered.
- Stage 7 gameplay may continue while this independent corpus provenance gap remains. No physical-PC, installation, Radeon or release work.
