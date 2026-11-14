# PLAY-13C.6B — Rare Frontier Oasis Prospects

## Recovered binding game-design decision

Remote frontier territory is naturally difficult to sustain. Rare **small oasis systems** can nevertheless occur where local conditions converge: **water, accessible energy, nutrients and physical shelter/radiation protection**. A planet-bearing oasis is generally the more favorable foundation; asteroid-only refuges remain possible as separate, more resource-intensive future systems. No oasis may spawn a staffed colony, food, breathable oxygen, free workers, ready-made infrastructure, free equipment or permanent safety.

**Oases are independent of ore wealth**. Exceptional mineral deposits must not be required to find an oasis, and oasis status does not make the surrounding rock precious. The player still needs to explore, survey, haul finite materials and construct farms, water recycling, atmosphere generation, power and shelters. A fertile potential site is not a self-sustaining settlement.

## Implemented: PLAY-13C.6B, observational prospect discovery

An authenticated celestial survey now reports exceptionally rare **potential** oases from the existing immutable physical astronomical catalogue.

- `Simulation::celestial_oasis_prospect(body)` derives a deterministic `CelestialOasisProspect` with four bounded binary prerequisites: water, energy, nutrients and prospective shelter. It accepts real catalogue body IDs only. The flags are **qualitative prospects**, not resource inventories or accessible prebuilt wells.
- The nominal system draw is `astronomy_mix(system ^ 0x0A515500) % 10000 < 200`, i.e. **2% of systems are permitted to have a candidate**, before eligible celestial-body availability. There is no promise that a particular region, sector, star system or frontier world contains one.
- At most one eligible candidate is selected from such a system. Planets are preferred; a qualifying moon may be selected if no qualifying planet exists. Stars and debris regions never become free safe havens under this initial rule.
- Planet prerequisites require a moderate orbital band; moon prerequisites are more restrictive. These are *game heuristics*, not scientific guarantees that a planet has liquid water or breathable air. A future site-level geological survey must confirm actual accessible mass before any resource may be removed or consumed.
- The prospect draw does **not** examine, depend on or modify mineral concentration, asteroid reserves, world danger level, credits or the player's industrial wealth. Systems need not become richer or safer because of proximity to a designated oasis.
- `Simulation::survey_oasis_prospects(owner,ship)` uses the existing scanner/ownership checks of an authorized astronomical survey and returns no findings to other owners. The live host emits `OASIS_PROSPECT|body|water|energy|nutrients|shelter` only as part of the legitimate `CELESTIAL` survey. The UI displays the potential with an explicit warning that it lacks constructed supplies or guaranteed safety.
- This is **pure deterministic projection of the persisted physical celestial bodies**. No new mutable conservation ledger or save-format version is needed; snapshots and reloads reproduce the same designations, with no seeding side effects.

## Native acceptance

`tests/PLAY13C_RareOasisTests.cpp` covers 1,200 systems: at most one candidate per system, sparse expected frequency, candidate's convergence of all four prerequisite fields, valid planet/moon body custody, authorized ownership, stable repeated assessments, canonical snapshot/reload identity and unchanged food, water, oxygen, asteroid resources and outpost counts. Windows and Linux native CMake workflow includes the test target.

**Unverified until CI actually passes.** PR #47 remains draft.

## Next physical gameplay implementation

1. Replace the binary survey indicators with **finite physically stored local resources** (measured water-bearing sites, nutrients, solar/geothermal/fusion fuel opportunity, protected terrain) and reversible, atomic geological source allocations.
2. Implement real settlement-support infrastructure in an actual planetary/moon frame: extraction and finite tanks, constructed power, greenhouse production, organic waste return and water recycling.
3. Prove loss/recovery and viability: an oasis cannot rescue colonists automatically when supplies run out, and a constructed colony cannot manufacture arbitrary water, power or food.
4. Extend only after the physical planet/moon implementation to exceptionally rare asteroid refuge clusters, which should require more extensive artificial protection.

Follow existing `docs/design/PLANETARY_AGRICULTURE_CLOSED_LOOP_NUTRIENT_RETURN_20261104.md`. A sustainable oasis must obey the same material conservation, recycling, resupply and waste-return rules as every other colony.

Keep common minerals accessible in safe-space. Frontier danger and life-support difficulty should arise from real distance, terrain, maintenance and lack of support, not an arbitrary exclusivity table.
