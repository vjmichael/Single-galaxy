# Single Galaxy — NPC lifespan and generational turnover (approved design decision)

**Recorded:** 2026-11-01
**Status:** Player-approved design constraint. Not yet implemented or benchmarked in the gameplay simulation.

## Core rule
- The **average NPC lifespan is 40 hours of active gameplay elapsed after birth**, in the proposed accelerated clock configuration of **one in-game year per one real gameplay hour**.
- This is a **population-level average**, not a hard 40-hour kill timer. Individuals vary, and accidents/illness/conflict can cause earlier death. Avoid artificial universal birthdays of death or scripted immortality.
- The design goal is **visible turnover and continuity across multiple generations in ordinary long-form play**. Players should meaningfully meet children, grandchildren, descendants and successors.
- The game-world simulation clock should remain consistent across near and cold regions; leaving a colony does not freeze its population. Confirm exact offline/paused behavior separately; do not count closed-app time by default unless an explicit feature enables it.
- **Family history, personal stories, inherited appearance, occupations and player reputation survive an NPC's death.** The dead NPC is not replaced by an identical copy. A deceased NPC remains in lineage/historical records.

## Important distinction
The source-code ART-01/ART-02/ART-02b appearance and family systems have been tested in limited contexts. A 40-game-year life expectancy, reproduction age, neonatal mortality, demographic distribution, labor replacement, migrations and playable-world birth/death integration at million-person scale **have not passed an end-to-end population stability test**.

## Required acceptance tests
1. Simulate an initially balanced population through at least 100 simulated years with the target mean life expectancy of 40 gameplay hours, then through longer horizons; verify distribution of age at death, not deterministic 40h expiry.
2. Establish a plausible fictional demographic schedule supporting 2–3 coexisting generations and viable adult worker recruitment; don't hardcode a population explosion to make births happen.
3. Feed age-dependent needs, education/training, working-age staffing, dependents, births, migration and deaths into existing NPC population and economic accounting with exact headcount conservation.
4. Validate generation continuity in `PersistentWorld` and cold-region snapshots with restarts and warm/cold transitions.
5. Confirm name, family memory, appearance traits, reputation knowledge, and one positive/one negative defining character trait persist or evolve for individuals without genetic personality inheritance.
6. Validate that the colony can survive aging and turnover economically, and that dead NPCs cannot continue working, hauling, consuming, or silently reappear.
7. Run accelerated 40-hour and 100+-hour equivalence tests plus crash/restart, compare cold and detailed simulation census.

## No claims about biology
NPCs are fictional modeled people. The chosen 40-year average lifespan is a **gameplay parameter** for generational turnover, not a scientific estimate or a claim about real populations.

## Other approved principles
- No arbitrary immortality or universal plot armor for ordinary NPCs.
- Default is consequences and material continuity rather than punishment: declining labor supply should be forecastable and manageable by player policies and NPC managers.
- Avoid forcing a dramatic dialogue every time a descendant meets the player. Knowledge of the player should require experience, family stories, records or plausible rumor transmission.
