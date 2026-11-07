# Single-Galaxy — Owner-directed 20-stage development sequence

> **Stage 3 now active (2026-11-06):** The owner authorized WO3 planetary colonies and operations while WO2 draft PR #88's native Windows/Linux acceptance remains open. WO3-A source adds original owner-approved facility construction proposals and physical site/NPC/inventory audits without treating orbital outposts as permanently housed planets. [WO3 ledger](WO3_PLANETARY_COLONY_OPERATIONS_20261106.md). WO1 source on draft #87 and WO2 source on draft #88 are not native accepted or merged.

> **Stage 1 progress:** WO1 source implementation is recorded on [draft PR #87](https://github.com/vjmichael/Single-galaxy/pull/87); its native CI acceptance is still pending. **Stage 2 has begun** on stacked draft PR #88, including native worker/profession source and WO2-G provisional coverage checks. Neither stage is merged/accepted as an installed release.
**Recorded November 6, 2026.** This supersedes earlier suggestions to prioritize full player gameplay, native 3D production or multiplayer ahead of the world simulation. **This is the order in which major feature areas should receive concentrated development work.** Existing prototype implementations remain useful foundations, not evidence that an area is finished.

## Binding order

| Step | Area | Main engineering exit criteria |
| --- | --- | --- |
| **1** | **Industry, real-world research and recipes** | Research real supply chains; define canonical item/material families, physical units, meaningful recipe dependencies, byproducts, manufacturing facility requirements, and NPC/user demand. Avoid exhaustive personal trinkets and pointless tier ladders. |
| **2** | **NPC work and management** | Map the approved recipe graph to actual tasks, professions, qualifications, equipment, capacity, training, shifts, maintenance, supervisors and owner-authorized work delegation. Reuse original `WORK-01` and native NPC records. |
| **3** | **Planetary colonies and their operation** | Determine permanent housing, worksite organization, food/utility consumption, migration/census, residency, buildings and player-authorized NPC construction using the resulting item/work taxonomy. |
| **4** | **Economy and resource distribution** | Consumption profiles, real supply/demand, allocation, scarcity, stockpiles and regional goods flow. A money/wage subsystem is NOT a prerequisite: user previously removed mandatory chits/wages. |
| **5** | **Connect economy to transport/logistics** | The physical fleet, shuttle and existing freight/custody network transports approved material classes, contracts and supplies with no teleportation or independent shadow ledger. |
| **6** | **Planet generation and targeted integration revisits** | Incorporate geologic material availability, water, environments and resource geography, then repair grounded prerequisites in stages 1–5 as required. Planet class constrains regional environments, not one generic biome per planet. |
| **7** | **Population and society** | Integrate item/service access, traits, comfort, health, expectations, trust, culture, migration, households and scalable cohorts. NPCs react to persistent conditions; a single missing chair does not trigger immediate migration. |
| **8** | **Galaxy and space travel** | Real space geography, routes, infrastructure, risk and navigation; maintain single-galaxy scope and physically built/maintained transportation infrastructure. |
| **9** | **Combat and ship security** | Existing physical combat/ship status/cyber/agency source is a foundation; complete tactics, crew effects, evidence, legal authority, custody, rescue and physical security resource requirements. |
| **10** | **Begin simple 3D asset development** | Low-complexity placeholder-replacement geometry for the now known item and facility families; choose reusable shapes/components, no expectation of final polish. |
| **11** | **Player gameplay** | Derive actionable activities and user flows from the established world, rather than inventing unsupported resource or labor activities. |
| **12** | **Increase 3D asset coverage** | Expand visibility and identity for game-useful facilities, ships, planets, tools and NPCs. |
| **13** | **Multiplayer** | Invite-only players with explicit original-world authority, physically consistent ownership/inventory, robust native sessions and disconnect/reconnect. No general trading mechanic added merely to support multiplayer. |
| **14** | **Performance and persistence** | Wide-area scaling, cohort/detail scheduling, storage upgrades, long-running simulation, crash safety and rollback/recovery under load. |
| **15** | **Increase 3D asset coverage again** | Improve interaction-specific geometry, animation and asset variation. |
| **16** | **Major build and test cycle** | Integrated native executable/client and complete-system acceptance, Windows/Linux where applicable. |
| **17** | **Finish remaining 3D asset production** | Release-directed art, animation, effects, presentation and consistency. |
| **18** | **Second major build and test cycle** | Regress complete game with finalizing assets, installation, saves and player loops. |
| **19** | **Final performance check and testing** | Long-duration load, capacity, crashes, native installed-game and user acceptance; repair real failures. |
| **20** | **Release** | License/provenance, distribution, reproducible artifacts, documented limitations and release candidate acceptance. |

## Scope discipline

- **Steps are development priority, not a ban on prerequisite testing.** Run native compile, unit/integration tests, material conservation, original-owner security, finite-world accounting, Save/Restart and schema migration with EVERY committed architectural change; stages 16/18/19 are broader milestones rather than the first tests ever conducted.
- Player-facing gameplay work is **deprioritized** until stage 11; small workbench/admin viewers may be used as engineering inspection interfaces while developing industrial semantics. Do not pretend those are the release-ready game.
- **Step 10 is the first intentional art-production wave.** Programmatic diagrams, primitive debug geometry and engineering test fixtures beforehand are not a separate art roadmap. No packaged/installed Windows 3D acceptance surrogate should be substituted with a browser.
- The **one original galaxy** begins with a single colony ship and finite crew; player-established infrastructure takes real material and time, never spontaneous NPC invention. NPC construction may execute only the owner's chosen projects or specifically authorized standing programs.
- Existing original-world `Simulation`, `PersistentWorld`, SQLite, factory-workshop `EconomyPrototype`, `Inventory`, `WorkPlanning/WorkCapacity`, NPC flight and transport systems should be extended, **not rewritten in competing parallel systems**.
- Do not let old `ECON-01` document language prescribing wages, credit or chits override the newer owner choice: no mandatory wages and no game-economy currency. Resource contribution/consumption and physical allocation come first.
- This roadmap does **not** mark earlier stacked PRs or the current security PR #86 as tested/merged. Native verification remains open and may proceed independently without stopping industry research and implementation.

## Stage 1 — industry research program

Research primary real-life value and supply chains across:
1. food (crop farming, ranching, food processing, storage, retail, restaurant),
2. medical and health,
3. construction/materials/housing and furnishings,
4. electric power, fuel/energy carriers and utilities,
5. space/transport infrastructure and vehicle maintenance,
6. communications/computing/electronics,
7. entertainment goods and venues,
8. services (laundry, sanitation, maintenance, legal, education, hospitality),
9. government administration, records, emergency services,
10. horticulture and household comforts (including real nursery/houseplants).

First identify common inputs and shared industrial operations; *then* identify meaningful item classes; *then* write bounded recipes; *then* map work roles, skill sets and NPC access/consumption to graph nodes. Durable possessions generally provide ongoing comfort/use/maintenance and eventually wear; they are not destroyed every NPC tick merely to generate demand. Consumables and services have separate use rates.

**Source/research brief:** [INDUSTRY_SUPPLY_CHAIN_RECIPE_RESEARCH_20261106.md](INDUSTRY_SUPPLY_CHAIN_RECIPE_RESEARCH_20261106.md). Stage 1 finishes a verified, catalog-backed, non-duplicative recipe specification with sample closed resource loops, explicit abstraction and physically meaningful outputs; its full NPC scheduler implementation is stage 2.
