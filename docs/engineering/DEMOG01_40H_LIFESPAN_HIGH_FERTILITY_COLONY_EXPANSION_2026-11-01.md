# DEMOG-01 — 40-hour lifespan, fertility and starter colony size exploratory simulations

**Date:** November 1, 2026
**Status:** Completed standalone exploratory Python cohort/makeshift-galaxy study. **No native Single Galaxy gameplay changes, no PersistentWorld or warm/cold acceptance claim.** Source, test logs, graphs and all CSV evidence delivered in the conversation artifact `SingleGalaxy_Demographic_Colony_Balance_Study_2026-11-01.zip` (not committed as source here).

## Purpose
Investigate proposed **1 game year per active player hour**, mean NPC lifespan **40 years / 40 active hours**, and **3–5 target children per participating couple**, allowing initially uninhabited finite planets to be colonized through physically funded daughter colony ships whenever overcrowding, dwindling reserves, or blocked local construction create stress. Compare founder ships of 2,000, 5,000, 10,000, and 20,000 residents.

## Assumptions and constraints
- 72 deterministic candidate settlement sites with geology, differing farm/water productivity, finite mine ore, buildable habitat limits; only one starter settlement exists initially.
- Adult reproduction permitted for age **18–31**. Partner participation 82% baseline, which is a statistical cohort rate, not actual assigned couples. Births require resources. Variable age-at-death modeled with approximate mean life 40 years, not hard kill at 40.
- Finite housing, food, water, mine/industry capacity and colony-ship construction materials. Actual origin population/cargo subtracted at departure; max 1,500 settlers per ship, one modeled game-year travel, and no free residents at destination. No space gates/stations magically prebuilt.
- These are **abstract economy proxies**: not complete mined-mass accounting, actual C++ native AI, real NPC family genomes, actual food/industry recipes, ship navigation, or renderable detailed NPCs.

## Study scale
- 180 simulations: 4 founding sizes × 3 fertility targets × 3 lifespan targets (35/40/55 years) × 5 geographic seeds, **160 simulated years** each.
- 90 320-year longevity extensions.
- 81 environment/family-participation/ship-material-cost sensitivity runs.
- 10 matched expansion-enabled vs disabled comparisons.
- Eight focused Python invariants passed. Population ledger, transit population, material consumption and finite colonization constraints were checked; same inputs reproduced identically.

## 10,000 starter settlers, 40-year mean lifespan, 160-year results
Median across 5 seeds:

| Target births per participating couple | Population at 160 years | Colonies | First daughter colony | Food deficit share | Water deficit share |
|---|---:|---:|---|---:|---:|
| 3 | 16,461 | 1 | None | 0% | 0% |
| 3.5 | 42,297 | 6 | year 112 | 0% | 0% |
| 3.75 | 63,840 | 7 | year 75 | 0% | 0% |
| 4 | 76,683 | 7 | year 56 | 0% | 0% |
| 5 | 90,918 | 8 | year 34 | 2.606% | 3.252% |

The 4-child scenario **still had about 4,300 people in a modeled housing backlog at year 160**. No food deficit does not mean no population stress.

Alternative starting ship sizes with 4 children/life40: 2k→19,516 population/3 colonies/first expansion year149; 5k→48,089/6/year82; 10k→76,683/7/year56; 20k→108,979/8/year45. A 10,000-person original ship implies a very large, expensive ark and corresponding bootstrap supplies in gameplay.

## Long-term and robustness
- Same 10k/4-child condition at 320 years: median ~341,589 inhabitants/23 colonies, but **1.176% cumulative food and 1.078% water demand unmet**. This needs better labor/infrastructure planning; not indefinitely sustainable yet.
- Five-seed 10k/4-child model with settlement construction suppressed: 45,504 instead of 76,683 residents by year160, only one colony, some resource shortages.
- Alternate productivity, adult partnership rates, and ship costs gave 30,422–114,675 population and 1–12 colonies for the 10k/4-child setting. The chosen design cannot be treated as universal from one toy simulation.

## Provisional design recommendation
**Retain the user-approved 40-hour average lifespan. Trial 10,000 settlers on a deliberately large colony ark, average target 4 births per participating couple, adult reproduction only, and an explicit industry-funded early-warning daughter-colony program.** If this is too much for the intended starting ship/early colony UX, 5,000 founders is a valid lower-pressure alternative, but the first new colony forms much later with fixed assumptions.

Do **not** permanently lock exact fertility, ship size, age distribution, or birth schedules into production from this study. First validate a common aging clock, real consumption, dependent care, housing, workforce training, finite local mining, migration ships, and durable population transfers in the C++ `PersistentWorld` / regional executor. Then re-run equivalent scenarios across cold/warm switches, saves and replays.

The experiment intentionally favors a viable initial planet. Climate failure, epidemics, piracy, random ship loss and year-round family choices remain future experiments. High fertility without colony infrastructure can be punishing; that should become a forecastable, governable system, not an arbitrary surprise.
