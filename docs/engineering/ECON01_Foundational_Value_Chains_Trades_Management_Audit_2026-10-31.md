# ECON-01 — Foundational Value Chains, Trades and Management Audit

**October 31, 2026 · architecture/backlog only · no gameplay implementation claimed by this audit**

## Key assessment

Single Galaxy already has a **partially connected physical resource backbone** (mining, refining, limited fabrication, player-founded outposts, staffed agriculture, utilities, habitation, waterworks, maintenance, clinic and elementary local provisioning). It **does not** yet implement a complete, autonomous, market-coordinated, multi-profession economy. Avoid suggesting that its present fixed-bid station test market is already a working economic system for the no-infrastructure colony start.

Preserve the player premise: one original colony ship, finite workers and supplies, no prebuilt markets/refineries/gates/stations; early shipboard bootstrapping, physically local inventory, no teleporting cargo, meaningful scarcity, no daily-login burden. Recipes and rates are intentionally game-balanced prototypes, not real-world engineering performance.

## Verified/implemented foundation, with qualification

| Domain | What has executable evidence | Major gap |
| --- | --- | --- |
| Geological mining | Finite asteroids, named mineral composition, cargo, source receipts | General NPC autonomous extraction, industrial mine throughput, grades/survey uncertainty |
| Refining/material processing | Named mineral phases -> alloy/aluminum/ceramics/glass/raw water etc.; residue, mass-accounted recipes | Industrial production lines, varying yield/quality, utilities and consumable machinery |
| Fabrication | Structural modules built from real alloy/aluminum; modules embodied into facilities | Component bill of materials, tools, machine shops, multiple factories and repair part classes |
| Construction | Foundational ship -> outpost, then farms, housing, power, clinic, waterworks etc. | Project stages, specialized trades, skills, scheduling, construction equipment |
| Agriculture | Site-local inventory, farm worker headcount, seed/crop periods, food, waste, consumption | Crop diversity, livestock, advanced food processing, transport contracts, wider labor roles |
| Power/water/oxygen | Built solar, repair wear, recycling, waterworks feed and measured rolling windows | True renewable net system, distribution networks, replacement spares, realistic reliability |
| Hauling | Ship physical cargo, station hangars, local transfer requirements, multi-stop travel/gates in legacy fixtures | Named drivers/pilots, NPC delivery jobs, route dispatch, contracts, warehousing networks |
| Economic exchange | Local station market fixture with fixed bid, wallet and inventory; buy/sell transfer testing | No endogenous supply-demand markets, player-founded exchanges, wages, contracts or multi-good price discovery |
| Named people | Finite persistent identities, productive farm XP/mentoring, named mining work with command/effect receipt | Most workplace headcount is aggregate; named worker scheduling, trade qualification, employment |
| Delegated operations | Maintenance policies with finite spares/assigned technician capacity and limited policy inspectors | No individual skilled repair receipts, budgets, order delegation, real managers, autonomous procurement |
| Quality of life | Habitat, clinic, community, food, water, coverage/resilience diagnostics | Personal needs/health, leisure outcomes, safety, education, quality-of-life surplus, credible managed absence |
| Macro society | Persistent groups, overlapping memberships, testimony, reputations and evidence-linked interpretations | Organization governance, legal systems, institutions, budgets, cross-group decisions and NPC economy |
| Scope/platform | Local Linux bounded 2D prototype with durable SQLite and limited browser UI | Continuous 3D galaxy, production network server, Windows acceptance, full integrated manual play |

**Evidence:** [B7.0a](../B7_0a_Colony_First_Core_Loop_Report_2026-10-30.md), [B7.0c](../B7_0c_Mineral_Industry_Report_2026-10-30.md), [B7.0e](../B7_0e_Player_Built_Agriculture_Managed_Crew_Report_2026-10-30.md), [B7.0h](../B7_0h_Delegated_Maintenance_Report_2026-10-31.md), [B7.0l](../B7_0l_Mineral_Water_Replenishment_Report_2026-10-31.md), [B7.0n](../B70n_Persistent_Workforce_and_Cohorts_Report_2026-10-31.md), [GRSM-04b.B1](../GRSM04bB1_Named_Mining_Execution_Report_2026-10-31.md). Primary actual code: `include/galaxy/Simulation.hpp`, `include/galaxy/Persistence.hpp`, `src/Simulation.cpp`, `src/Persistence.cpp`.

## A minimal useful interdependent value chain

```text
Survey -> finite mineral deposit -> mining -> ore cargo
                                   |
                               refine/separate
                                   |
            +----------------------+--------------------+
            |                      |                    |
        structural metals      water/nutrients     residues/byproducts
            |                      |                    |
       machinery/parts*        purification          reuse/disposal*
            |                      |                    |
        construction        habitat/agriculture <----- energy/utility
            |                      |
      working facilities <--- food/oxygen/water
            |                      |
        maintenance* <-------- surviving workforce
            |                      |
          output ------------> stores/transport* ---> users/buyers*
```

`*` denotes incomplete or future links. Avoid presenting a no-infrastructure starting world as already owning machinery, transport fleets, wholesalers, or markets. A value chain becomes **operationally closed** only when it can replenish worn tools/equipment, supplies, working capital, and labor over realistic windows without unearned inputs. The present prototype deliberately withholds autonomous-growth certification.

## Essential occupational families to model before hundreds of professions

1. **Resource work:** prospector/surveyor, miner, material separation/refining operator, forestry/biomass extractor if later supported.
2. **Agrifood:** grower, irrigation/pump operator, food processor, storekeeper and sanitation worker.
3. **Production trades:** fabricator/machinist, welder/metalworker, carpenter/structural assembler, electrician, plumber/pipe fitter, mechanic/millwright; specialty professions should emerge as capability bundles, not require a different hard-coded engine for every job title.
4. **Infrastructure/transport:** builder/rigger, dock worker, cargo handler, ship operator, mechanic, dispatcher, warehouse/supply-chain coordinator.
5. **Care/community:** medical worker, environmental/utility operator, instructor, safety and basic services worker.
6. **Coordination/management:** team lead/foreman, maintenance planner, purchasing/logistics planner, production manager, administrator, treasurer/finance, safety inspector.

A job is an action at a site; a trade is a *set of capabilities*. A person can possess several capabilities, work for overlapping groups, learn by actual productive experience and mentorship, and choose to refuse/accept orders when future autonomous decision models are added. Do **not** introduce character level locks for flying ships or accessing the universe.

## Missing shared economic primitives (cross-cutting priorities)

- **Job/production order:** authoritative issuer, objective, worksite, inputs, target output, worker(s), necessary tool/machine/capacity, schedule, owner, execution effect receipt, failure/cancel reason, replay behavior.
- **Bill of materials and routings:** recipe input/output quantities, power/time/labor/tool use, multi-stage dependencies, quality/damage, wear, byproducts and recycling; audited actual resource movement instead of free crafting.
- **Worker competence:** persistent named-worker roles and multi-skill proficiency by productive work; on-the-job mentoring, safety, qualifications, physical fatigue/rest and learning curves, not one global character level.
- **Tools/equipment:** inventoried production tool assets, condition and repair material requirements; construction equipment, welding units, pumps, piping, machine tools. Specialist performance must depend on actual workplace resources, not only a numeric trait.
- **Storage/logistics:** local storage capacities, reservations, loading/unloading, shuttles/haulers, actual route/travel, dispatch, transfer receipts, delivery failure, leakage/loss, priority and fallback orders.
- **Accounting/markets:** physical ownership and contracts, procurement requests, inventory valuation, labor compensation, demand and stock replenishment, local prices, credit limits and solvency. Add prices only where real sell/buy agents and stock exist, not universal magical shops.
- **Management:** assignment and resource authority, budgets, delegated policies and escalation, bottleneck recognition, output quality, competing obligations, legal/social feedback, staffing shortages. Manager skill should improve bounded real outcomes over time, not instantly improve every worker in a colony.
- **Organizations:** workforce membership versus legal employer, guild, household, colony, society/federation; each may overlap while opinions and authority remain separate.
- **Resilience and maintenance:** production replacement cycle for real spares, water/oxygen/power continuity, waste capture, healthcare and safety capacity; credible long-window 'managed absence' not logout punishment.

## Recommended incremental gates

| Order | Gate | Smallest demonstrable loop | Acceptance protection |
| --- | --- | --- | --- |
| 1 | **GRSM-04b.B2 / ECON-01A** | Named fabricator (or repair technician) uses owned materials, local worksite, tool/machine capacity, power/time and returns a **verified committed product or repair** | Distinguish order-giver, assigned worker and executed effect; no output on failure; persistent receipt |
| 2 | **ECON-01B** | Shared job/order ledger + capability tags across mining, farming and fabrication | No worker double booking, no teleported goods, no instant untrained specialization |
| 3 | **ECON-01C** | A small working trade cluster: fabricator/metalworker, electrician, pipe fitter, mechanic maintain one outpost's power/water/greenhouse | Qualified job + physical spare/tool + actual equipment service outcome |
| 4 | **ECON-01D** | Named hauler delivers an input batch from ship/outpost A to B; inventory at both sites updates only after arrival | Transport capacity, route/time, loss/cancel/restart, provenance and ownership |
| 5 | **ECON-01E** | One production planner orders a real missing spare, reserves material and schedules worker + hauler | Bounded authority, limited capital, no free automatic procurement or labor |
| 6 | **ECON-01F** | Owner-funded local trade contracts, stock-dependent bids/asks, wage expense and delivery settlement | Conservation of goods and money, no ghost market orders/market at genesis |
| 7 | **ECON-01G** | Multi-site resilience and long-window autonomous management assessment with player return briefing | Managed absence without punishment; no uncontrolled colony growth on subsistence |

**Minimum convincing vertical demonstration:** a colony's utility pump wears out; a technician cannot repair it without the right part. A named manufacturing worker creates the part using local ore-derived metal, a hauler delivers it, a trained mechanic installs it, waterworks resume, farmers produce food and the colony's verified readiness improves. Each transition is physically committed, creates source receipts and is resilient to interruption/restart.

## Deferred major systems

Passenger logistics, fully physical construction interiors, sophisticated research/technology, trade finance and insurance, generational population/families, justice/policing, large-scale interstellar immigration, macro-group government, AI-driven corporate behavior, combat economies, truly continuous 3D space and private-server multiplayer are meaningful eventual additions, not prerequisites for the first working self-supporting colony. The server/launcher transport decision remains [NET-01](NET01_Deferred_Binary_Transport_And_Dual_Mode_Launcher_2026-10-31.md), deferred.

**Development philosophy:** implement reusable occupation/capability/work-order primitives first, then add concrete trades without proliferating bespoke engines. Prioritize an enjoyable player loop and optional delegation, not compulsory administrative chores. GitHub records decisions; runtime development and tests remain local-first.
