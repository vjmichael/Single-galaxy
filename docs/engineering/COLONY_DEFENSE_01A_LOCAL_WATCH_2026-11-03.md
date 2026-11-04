# COLONY-DEFENSE-01A — Finite local NPC watch and durable contact proof

**Stage:** v81, 2026-11-03 Arizona time. **Parent:** v80 real NPC habitat resupply and DEF-01 design contract.

## Exact implemented scope

`colony_defense::observe_local_contacts` is a deterministic query over the existing canonical `Simulation`. It requires an **actual colony ship** with a **living duty-eligible named NPC physically on that ship**, no active ferry transport, the opt-in physical passenger life-support rules, and a nonzero owner. The NPC's contact horizon is currently a bounded gameplay visual range of 5000 millimeters. A potential contact must be an actual other player-owned **Ship** entity, in the same star system, with different owner and within squared real position distance (bounded to avoid overflow). Results are naturally sorted by stable entity ID.

A detected contact is explicitly **unknown**, not automatically hostile. An observer does not gain line of sight to every galaxy ship via the public `entities()` access: the native helper filters before returning results, and there are no damage or attack commands in this module. Wrecks, asteroids, same-owner friends and out-of-range ships are not contacts.

A trusted host may call `PersistentWorld::record_colony_watch_contact` for one contact actually returned by this query. The existing world's SQLite writer starts `BEGIN IMMEDIATE`, rechecks the same world generation, and appends exactly one immutable event row to `sg_colony_watch_contacts`: event ID, generation, hash, physical tick, source colony, actual observer NPC, observed foreign ship, original owner, system, and integer squared distance. The procedure never advances the game tick, changes NPC knowledge without a subsequent explicit GRSM observation, mutates ownership or declares an attacker.

Normal restart and `recover_copy` call `Store::verify_colony_watch_contacts`, which independently reloads each pinned original world snapshot, verifies the checksum/world hash and tick, recomputes local NPC watch eligibility and the complete actual geometric contact, and refuses a row whose foreign owner or location provenance diverges. Both immutable write triggers must be present; restoring triggers after privileged tampering does not make bad evidence valid. Exact observation-generation snapshots are pinned by the canonical world-snapshot pruning policy.

## Acceptance fixture

`tests/COLONY_DEFENSE01A_LocalWatchTests.cpp` creates a named five-NPC colony ship, one same-owner nearby ship, one nearby foreign ship, and one distant foreign ship. Only the real nearby foreign contact appears. The test refuses fabricated observers, friendly reports, distant reports, and duplicate immutable event IDs. It records a real local observation after 20 world ticks, advances to tick 120 to exercise historical checkpoint retention, restarts the world and performs independent `recover_copy`. It then deliberately rewrites the foreign-owner field on a separate recovered copy, reinstates the original immutable trigger, and requires recovery to fail closed.

Focused CI additionally reruns `galaxy_colony_auto04d5c1_npc_resupply` to ensure the new contact evidence has not disrupted existing physical emergency supplies.

## Explicit limits / next defense gates

This is a **sentry sighting**, not a sensor array, weapons lock, trespass judgment, security reassignment, target classification, turret, player attack, shield, armor damage, crowd shelter, or verified casualty mechanism. The physical observation range is a preliminary local-crew visual range, not a balancing promise about final starship radar. Future **DEF-01B** must construct and power sensors, defenses and ammunition stores from finite world resources; **DEF-01C** must admit only authenticated physical impact/damage and protect against instant colony erasure; **DEF-01D/E** must coordinate real security staff, shelter/repair/resource-backed evacuation.

The player can eventually attack a colony, but these actions may not reuse `HostDestroyShip` to delete a populated colony or create casualties without a world physical-cause receipt and appropriate ART02+ECON03 population reconciliation.

**Focused acceptance PASSED:** [GitHub Actions run 41777022780](https://github.com/vjmichael/Single-galaxy/actions/runs/41777022780), executable commit `d501bb140a39974fff2496f50a2e32b685fd2675`, 2/2 native CTests passed in 0.50 seconds: NPC local watch and v80 physical resupply. This documentation change does not alter executed C++.

**Wider PR regression checks remain separate**; leave the pull request draft until they finish and integrate DEF-01B/C with independently verified physical combat transactions.
