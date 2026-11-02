# DEMOG-02 — Smaller colony starts, small expeditions and soft settlement targets

**Date:** November 1, 2026
**Status:** Exploratory standalone simulator. Not native gameplay integration; not a locked balance decision.
**Supersedes:** DEMOG-01's **provisional recommendation** for a 10,000-person starter. The player's current direction is to scale down the starting ship and colony sizes; 500 founders is only a recommended alpha test preset, not yet approved as final.

## What changed
- Retain player-approved **40 gameplay-hour average NPC life**, proposed 1 simulated year per real active gameplay hour, variable individual life expectancy.
- Trial starter ships of **500 / 1,000 / 2,000 / 3,000 colonists**, compared against 3 / 4 / 5 target children per participating couple.
- Switch from giant 350–1,500-person transport loads to **45–240-person physically funded pioneer missions**, with proportional ship/landing resources.
- Introduce **soft NPC settlement-planning targets** of 350 / 800 / 1,500 people, *not hard city limits*. Player may build and expand freely subject to real geometry, resources, utilities, workforce and ownership.
- Experiment uses synthetic 72-site galaxy with finite ore, food, water, housing, and paid ships; 82% simplified partnership participation, age-18-to-31 reproduction, and cohort/aggregate populations.
- Correction to exploratory earlier code: arriving ship passengers age once during their transit year, **not twice** on arrival.

## Simulated results
180 geography/fertility/founder/target combinations at 160-year horizon across five seeds; 18 longer 320-year runs; ten matched early-planning vs reactive runs. Eight automated model tests passed; deterministic repeatability and population conservation verified.

For **four target children per participating couple**, **800-person soft daughter-settlement goal**, 40-hour average lifespan:

| Starting crew | Colonies at 80h | Colonies at 160h | Total population at 160h | First new colony in reactive policy |
|---:|---:|---:|---:|---:|
| 500 | 5 | 13 | 4,726 | 44h |
| 1,000 | 6 | 20 | 9,402 | 32h |
| 2,000 | 9 | 24 | 17,830 | 32h |
| 3,000 | 10 | 26 | 22,889 | 32h |

Medians over five procedural world seeds. Food shortages are near-zero for the 500/4-child/800-target baseline; later long-horizon runs can incur supply shortages and cannot be deemed permanently stable.

500 founders, 800-person daughter-settlement target, 160h, fertility:
- 3 children => ~820 residents, 2 colonies, first new colony ~155h.
- 4 children => ~4,726 residents, 13 colonies, first new colony ~44h.
- 5 children => ~15,524 residents, 26 colonies, ~0.5% modeled food/water demand shortfall, ~705 residents underhoused.

If NPC planners anticipate family growth rather than waiting for stress, **500 founders + 4 children** can launch the first daughter colony by ~26h; at 80h ~7 colonies, at 160h ~15 and ~4,709 residents. Early planning does not hard-limit city sizes.

## Provisional design recommendation
Consider an alpha trial of **one 500-person colony ship, average target four children per participating couple, mean 40h lifespan, ~80–150 pioneers per expedition, and NPC-managed daughter settlements initially aiming for several hundred residents**. Do not fix the exact numerical targets until tested against the real C++ economy/household/birth/death and warm/cold world scheduler. A 1,000-person starter is the less intimate alternative.

## Verified evidence
Local reproducible source, run logs, eight passing tests, raw CSVs, charts and README: `SingleGalaxy_DEMOG02_SmallColony_Balance_Simulation_2026-11-01.zip` (conversation deliverable).

**Limitations:** No actual NPC couples, personal genetics, personality transfer, player-interface controls, spatial colony construction, actual world generation, native NPC workforce economy, combat, ship-loss hazards, or full physical mass balance. Numerical outcomes are assumptions-dependent; don't claim actual gameplay run or proven stability.
