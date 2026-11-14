# PLAY-13C.6A — Frontier Geological Opportunity and Expedition Viability

## Implemented

The player should never have to cross a hostile region just to unlock common minerals. Outlying deposits may be desirable because of actual available mineral mass and composition, but operation there competes with the finite food, water, oxygen, crew and supply infrastructure already in Single-Galaxy.

An **authenticated read-only** `Simulation::assess_expedition(owner,ship)` now evaluates only the region in which a real ship is physically anchored:

- Sum **remaining real physical asteroid tonnes** from the immutable v73 body manifest, not hypothetical ore, an all-system aggregate or an unrevealed out-of-frame deposit.
- Sum recoverable nickel-iron and plagioclase mineral fraction in kilograms from the actual surviving deposits and their v74 taxonomies. These are material opportunities, **not** money prices, guaranteed profit, invented minerals, processing yields or rich-new-world entitlement.
- Count actual aboard crew/passengers/dependents using the same effective population as the native 20-tick `process_colony_needs` routine. Use current ship life-support inventory to compute *whole* remaining consumption periods for food, water and oxygen. Do not include promised deliveries, farm output not carried aboard, market orders or hypothetical replenishment.
- Report v74 body hazard as **observational information only**, not additional arbitrary damage or consumption. Return true for a local supply site only when an actual friendly player-built outpost occupies the ship's same local physical frame; distant stations do not count.
- A wrong authenticated owner, a ship without real life support, or a ship not yet in a real regional frame receives no assessment. The function does not perform a command, debit supplies or advance a tick.

The native host emits `EXPEDITION|body|ore_tonnes|nickel_iron_kg|plagioclase_kg|food_periods|water_periods|oxygen_periods|hazard|local_support` after a completed region arrival and upon replaying status. The validated browser contract shows real usable mineral opportunities alongside the existing life-support runway. This is **not** a predicted travel ETA, a mining profit rating, or a guarantee that transport infrastructure exists.

Native test `tests/PLAY13C_ExpeditionAssessmentTests.cpp` exercises unauthenticated ownership isolation, no-visit/no-oracle behavior, exact 14-tonne region stock and mineral fractions, independent 20/40/60-supply-period buffers, deterministic decrease after real mining and after a real 20-tick consumption cycle, accumulated physical organic/water/CO2 waste, repeat observation without mutation, and exact SQLite restart. Native Windows/Linux PLAY-13C workflow includes the acceptance target.

## Intentional limits and next gates

This step integrates **existing** actual geological opportunities and life-support strain; it does not fabricate richer asteroid types merely to push users to the frontier. A proper new deposit grading and finite territorial resource generator will require durable, separately audited geological source-of-truth records. Physical supply lines, escort danger, fuel, ship repairs, rescue availability, insurance and transport costs are still open development gates. These should emerge from physical logistics instead of arbitrary danger-zone resource taxes.

Geological rarity and concentration influence usefulness; production processing energy, local manufacturing demand and hauling risks determine realized value. All common industrial minerals must remain obtainable in safer space; extraordinary deposits may happen anywhere but their frequency can vary by geological history. Do not impose arbitrary region-specific resource locks.

**Native CI verification is required before merging PR #47.**
