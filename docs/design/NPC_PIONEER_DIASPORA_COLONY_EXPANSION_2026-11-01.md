# Single Galaxy — NPC Pioneer Diaspora and Settlement Formation (Design Decision)

**Date:** 2026-11-01  
**Decision:** Adopt organic NPC-led settlement expansion as a core simulation behavior. **Implementation remains gated.**

## Player-facing concept

Players begin aboard one bootstrap-capable colony ship with a finite NPC population and no pre-existing free colonies, stations, stargates, shipyards or manufacturing chains. As families grow and communities develop, NPCs may voluntarily establish daughter settlements when actual transportation, capital, supplies, candidate land and labor permit it. The player remains ultimate leader of their originally founded colonies, with policy authority and the ability to appoint or replace NPC administrators. NPC plans are recommendations or host-authorized delegated work, not a bypass of the player's authority.

## Approved direction, provisional numeric parameters

- NPC **average life expectancy**: **40 active gameplay hours**, assuming provisional speed 1 in-game year per active real hour. Death is stochastic, not fixed at hour 40. Offline progression and exact clock implementation are separate decisions.
- Target household size: **roughly 3–5 children per participating couple**, with **4** a provisional test midpoint. Not every NPC is paired; parenthood and family formation vary, and no biological personality/ideology traits are inherited.
- Move away from a proposed 10,000-person ark. **500 founding residents** is the leading alpha test preset, **not yet a permanently locked starting-population rule**. Alternatives of 1,000 or 2,000 are retained.
- Typical pioneer expedition trial: **80–150 actual colonists**, within tested range of 45–240 depending on housing/supplies/ships; not necessarily all a single household.
- Daughter colony planning sizes of **400–800 residents** are soft operational goals. **No artificial hard limit on colony population, factory floor, or expansion**; physical geometry, labor, water/food/air, energy, construction materials, space, economics and ownership are the actual constraints.
- The player's game is primarily a satisfying space sandbox: mining, manufacturing, combat, hauling and exploration stay the active loop. NPC population simulation enables the world, rather than becoming compulsory player micromanagement.

## Why NPCs found new settlements

NPC administrators periodically forecast available housing, food, water, oxygen, jobs, school/medical services, useful deposits, land, opportunities and migration interest. Triggers can include:

1. Growing household size and insufficient housing or future capacity.
2. Falling future resource reserve days (food, water, oxygen) or finite mineral depletion.
3. Better mining, farming, trade routes and available land elsewhere.
4. Settlement policies, family connections, migration opportunities, independence or voluntary work/education programs.
5. Strategic interest, emergency evacuation or changing local risk, when actual transport permits.

Predictive migration is preferred over waiting for famine. A shortage alone does not *create* a colony or justify starving the source by emptying its provisions.

## Authorization, physics, and accounting

A new settlement requires a discovered reachable site, source labor and physical goods, real launch capacity, manufactured ship or existing eligible vessel, travel time, risk, disembarkation and construction. Every expedition reserves supplies and manifests **before departure**; it cannot strand the source below emergency reserves without authorized policy. Residents are removed from source population only upon a durable launch, counted in transit, and added to the destination only upon confirmed arrival. Cancel/recovery/crash must not clone or erase people. Deaths in transit are accounted for; destroyed ships do not silently respawn crew. All material and population transfers require host authorization and durable receipts. No free NPCs, buildings or supplies. Settlements are individually tracked, with cold/faraway regions advanced in aggregate rather than per-person.

Do not infer the player's intent for every individual NPC. Delegation can act within explicit policy/budget/permission boundaries, while the player can inspect and override the colony program.

## Personality, identity and consequences

Founding families, descendants, physical inherited facial variation, social memories, professional training, relationships, faction opinions, local infrastructure and homes persist. Migration moves finite people with finite skills, not fresh anonymous replacements. Settlers may return, fail, relocate, create businesses or form new communities; remaining kin need not migrate together. Birth, death and family events are quiet by default unless important to the player.

## Provenance and implementation gates

- **DEMOG-01**: aggregate 10k starter demographic sweeps; provisional 10k starter now rejected as a good default.
- **DEMOG-02**: smaller-colony synthetic galaxy sweeps (500–3,000 founders, varied household size and soft destination target), with finite modeled pioneers and material budgets. Model results are hypothesis-generating **not production acceptance**. A representative 500 / four-child / 800-soft-target case produced about 4,726 people and 13 colonies by 160 simulated hours. Proactive expansion led to first new settlement around hour 26 vs 44 in reactive runs. These figures are sensitive to fertility, model resource assumptions, travel and geography.
- **DEMOG-03 next engineering gate**: local native, deterministic **non-authoritative** expansion-advice planner fed by real-like resource snapshots, with reasons, thresholds, reserve constraints and tests. Never equate planning output with permission to migrate.
- **DEMOG-04**: authoritative expedition escrow/launch/travel/arrival and population-transfer ledger in PersistentWorld and regional cold/warm ownership. Only then begin integrating births, actual local housing and resource consumption with the real economy.
- **DEMOG-05**: multi-seed large-scale population benchmarks, disaster recovery, GUI acceptance and player policy controls.
