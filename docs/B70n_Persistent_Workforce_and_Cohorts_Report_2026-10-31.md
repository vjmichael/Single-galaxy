# Single Galaxy B7.0n — Persistent NPC workforce and stratified population model

**Date:** 2026-10-31  
**Scope:** Small, real NPC identity/experience integration plus a separate scalable cohort arithmetic prototype.  
**Prior base:** Verified B7.0m policy-execution archive.

## Working code

- `Simulation::enable_individual_workforce()` is an **explicit, host-controlled genesis-only option**. It turns the finite starting crew and existing assigned farm/habitat counts into persistent numbered NPC records, without adding residents. It is not automatically enabled by the current game startup. A journaled migration for already-running v19 saves remains an open gate.
- NPCs currently have durable unique ID, home ship, present location, ship/farm/habitat role, farm XP, and optional mentor ID. Existing aggregate crew/housing/staff counts **remain authoritative** for consumption, capacity, and other services; NPC records must agree exactly with those counts on v20 restore.
- Existing authorized farm/habitat transfer commands now move the corresponding individual records when v20 is enabled. Movement clears relevant mentorship links. Personnel are selected in deterministic ID order for now; selecting named individuals in the UI is not yet implemented.
- Productive staffed farm harvests increase assigned workers' farm XP, not idle ticks. A qualified expert may mentor one less-experienced apprentice at the same farm via `Action::AssignFarmMentor`. The apprentice gains one bonus XP for each actual productive cycle while behind the mentor, and the mentorship ends on reaching parity. The action requires the colony player's authority and the nearby ship at the owned worksite. There is no magic staffing or remote training.
- Farm teamwork accrues only during successful multi-worker productive cycles. Personnel changes erode retained team experience by a provisional 1/3. This is a learning prototype, not a validated management or organizational-maturity formula.
- SQLite canonical snapshot **v20** saves NPCs, the NPC ID allocator, mentorship links, and farm team experience. All v1–v19 snapshots remain accepted by the existing decoder without retroactive version coercion. Restores reject count mismatches, illegal roles, out-of-colony locations, orphaned teams, impossible team experience, and multiple apprentices assigned to one mentor.
- Separate `PopulationCohorts` C++ module: each `(settlement, occupation, skill band, expectation band)` stratum has an unsigned-64 population count, transfer operations conserve the population, and bounded fixed-point migration-rate operations avoid multiplying huge counts by a rate. No per-NPC loop is needed for this aggregate arithmetic.

## Verification

- `ctest --test-dir build -j4 --output-on-failure`: **54/54 passed** in one complete run (including the new B7.0n native test).
- `node --test tests/*.js`: **84/84 passed**.
- `cmake -S . -B production_b70n -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DBUILD_BENCHMARKS=OFF` followed by `cmake --build production_b70n -j4`: **passed**.
- Native B7.0n test exercises 24 people, count conservation during transfers, productive-job XP, 1:1 mentoring, unauthorized mentorship denial, a failed second apprentice assignment, mentor cessation after departure, v20 round-trip, SQLite commit/reopen, copied recovery, altered-record rejection and mass conservation.
- Separate cohort test represents **1.3 trillion persons** in a few compact groups, moves 125 billion through a fixed-point migration calculation, retains subgroup differences, checks conservation and rejects impossible/invalid transfers. This is an arithmetic scale proof only. It does **not** prove tick performance for a trillion individual agents, trillion populated 3D spaces, economies, detailed demographic transitions, or storage/access at that scale.
- Build and regression logs are stored alongside this report in `docs/B70n_*.log`.

## Why cohort strata instead of one average person?

A single population average cannot preserve occupational shortages, high standards among stubborn residents, or uncommon events. For distant or very large settlements, retain compact **distributions by relevant attributes** and count flows, not only a grand average. Individual simulations remain appropriate around the player and for unique historical actors. A future importance-based simulation LOD system should convert between both representations with explicit conservation, uncertainty, and reconstruction rules.

Sources reviewed:
- *Agent-Based Models in Empirical Social Research* (2013), discussion of aggregate simulation tradeoffs, spatial interactions, and lost heterogeneity: https://pmc.ncbi.nlm.nih.gov/articles/PMC4430112/
- Hosszú et al., *The optimal choice of scaling in economic agent-based models* (2025), sensitivity to scaling and distributional information: https://doi.org/10.1016/j.jebo.2025.106928
- AnyLogic documentation, combining population-scale system dynamics with individual agents: https://anylogic.help/anylogic/system-dynamics/combining.html

## Explicit limitations / next gates

1. v20 individual records are **opt-in on genesis**. Do not automatically enable them in a live legacy v19 world: a journaled, transactional save migration and recovery test are required first.
2. Current individual roles include only `Ship`, `Farm`, and `Habitat`. Clinicians, maintenance technicians, inspectors, transport crews and community workers are still aggregate assignments, even where they draw from resident capacity.
3. The cohort module currently runs independently. It is not the live galaxy-wide population system; no births, deaths, economic exchange, expectations-based decisions, or actual travel are implied by a cohort transfer.
4. No online multiplayer, 3D space, Chromium click-through, or Windows-native verification in this milestone.
5. No NPC autonomous job selection, professional workforce administration, institutions, personality traits, formal training curricula, labor unions or migration policies have yet been integrated with the new identity model.
6. Formal management proficiency, team organizational maturity and realistic adjustment times need separate state and tests. An expert individual must never instantaneously transform the organization.
7. Add a validated foreground/nearby individual-agent scheduler and offscreen **bounded, stratified population update**, with promotion/demotion between fidelity levels and tests that quantify discrepancies and rare-event behavior.

**Status:** B7.0n foundation implemented and Linux-verified. Entire NPC workforce/autonomy milestone is **not** complete.
