# Single Galaxy — B7.0i Resilience and Quality-of-Life Audit

**Date:** October 31, 2026  
**Scope:** Deterministic, read-only resilience measurement in the existing bounded 2D Linux simulation. **Status:** Implemented and tested. Not a finished game or autonomous NPC government.

## Engineering objective

B7.0h proved that NPC technician slots can maintain a physical utility with finite locally delivered spare parts. B7.0i must make the next decision safer: distinguish mere surviving/habitation from potentially dependable **day-to-day operations**, and explicitly forbid declaring a colony self-sufficient or safe to expand without implemented proof of quality of life and resources.

## Implemented

1. **Native `Simulation::assess_resilience(ship)`** — independent read-only assessment using persisted material inventories, local crew assignments, three-period food harvest history, service reliability and maintenance policies. No world mutation or bypass of player authority.
2. **Per-site reserve checks** — six 20-tick needs intervals of actual food, water and oxygen for the crew remaining aboard and for the local residents and farm workers at **every occupied outpost**. One well-supplied site cannot mask another site's shortage. The six-cycle buffer is a configurable-future diagnostic design threshold, not an enforced player requirement or technology lock.
3. **Repair and staffing margin** — every occupied site needs locally functioning power for at least three completed service periods, a standing maintenance policy, at least one resident technician, **two locally stored spare modules**, and at least half of nonfarming residents available outside maintenance assignments. This is a transparent competing-labor **headcount proxy**, not an individualized shift planner or real leisure model.
4. **Food production buffer** — requires a recent operational farm and food harvested in **each** of the last three completed needs periods at least 125% of the total current population's per-period demand. A large single harvest or a founding stockpile cannot hide recurring production shortfalls.
5. **Typed blocker mask (11 named reasons)** — no occupied site, low ship or site reserves, recent shortages, power, maintenance, overcommitted technicians, food-buffer failure, and three deliberately unverified areas: renewable water, healthcare/rest/leisure, and NPC growth policy. No synthetic success for unsupported services.
6. **Explicit two-stage interpretation:** `operational_delegation_candidate` can become true if the currently implemented physical/throughput checks all pass, but **does not enable new autonomous actions**. `autonomous_growth_ready` remains **false**. The historical `ColonyViability::Livable` is not silently redefined as true quality-of-life certification.
7. **Matrix Vision telemetry/UI:** C++ host emits `RESILIENCE` frames, strict JavaScript parser rejects malformed/contradictory claims, and existing local interface displays margins and clear bottleneck text. There is **no client-side authorization endpoint** for granting growth or new operational powers.
8. **Durability:** the diagnostic is derived from canonical saved records, so **SQLite snapshot v14 remains unchanged** and old v1–v14 restoration is not invalidated by a new stored field.

## Native test situations

- No built infrastructure must report no occupied site and missing food-buffer proof.
- Buildings without enough local provisions cannot be marked resilient even when ship stores are ample.
- Authorized physical deliveries, real resident assignments, three cycles of local services, food buffer, and parts can yield a **candidate** for future operational delegation, *not* growth authorization.
- Assigning two out of two residents to maintenance flags overcommitted labor and removes the candidate.
- Removing manufactured spare modules flags inadequate maintenance and removes the candidate.
- A ship flies using real acceleration and deceleration, constructs a **second** outpost, provisions only four local needs intervals, houses a resident, and leaves it without solar service: the new site's limited reserves and absent power block delegation even while the first site has substantial local supplies.
- Material mass remains conserved, and diagnostic fields match after canonical snapshot parsing, SQLite restart, and independent copy recovery.
- A live C++ host emits frames, resumes its saved colony, and a genuine local relay serves the new interface assets; the browser decoder rejects invalid frames and false growth claims.

## Verification

- Linux CTest: **43/43** native/integration suites passed, including the B7.0i native and live-relay suites.
- Node.js: **69/69** JavaScript tests passed, including two B7.0i parser/UI tests.
- Linux release build without testing/QA: **passed**.
- Source archive was unpacked in a clean directory and recompiled; the two new native/integration tests and Node contracts were rerun from the extracted files. Results and source SHA-256 are delivered with the archive.

Reproduce from project root:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure -j4
node --test tests/*.js
scripts/run_colony_linux.sh build/galaxy_colony_live_host
```

## Limits and following work

- **No offline catch-up while a private server is stopped**, return-briefing history, NPC budgets, full job scheduling, transport couriers or personal skill levels. B7.0h's standing repairs continue only while the simulation ticks.
- **No adequate evidence of true closed-loop water/oxygen replacement** (the existing accumulated water-recovery counter is not a recent-time-window throughput proof). No player-built clinic, actual rest/recreation service, healthcare resource flow, genuine satisfaction metrics or NPC governor.
- No autorun expansion, no NPC population spawning, no forced daily login, no implicit inactivity penalties, and no provision of free inventory.
- Historical provisional-power saves remain compatible; they cannot pass the new strict resilience check without actual utilities.
- Linux headless/native and protocol-tested only. Not verified on native Windows, with actual Chromium click-through, or in continuous 3D.

**Recommended B7.0j:** implement physically constructed healthcare/rest/recreation services and recent-rate closed-loop water/oxygen measurements with explicit local production/consumption; then add local budgets, crew shifts, and paused-server/fast-forward policies bounded by material stocks. Any autonomously managed growth must depend on sustained, verified quality of life above mere subsistence.
