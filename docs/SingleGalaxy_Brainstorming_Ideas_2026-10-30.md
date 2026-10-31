> **CURRENT DESIGN REFERENCE (2026-10-30):** [SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md](SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md) supersedes this early B5-era brainstorming/roadmap document. This file is retained as an **historical decision record**, not the current milestone plan.
> **Current priority:** B7.0c mineral-specific extraction/refining and usable components; then agriculture, NPC needs, colony viability and managed absence. See consolidated spec for confirmed decisions versus future features.

# Single Galaxy — Design Brainstorm and Decision Register

**Date:** October 30, 2026  
**Status:** Living design document; conceptual brainstorming, **not** a claim that these features are implemented  
**Scope:** Original single-galaxy space simulation/game. EVE Online and EVEmu are research references, not code or content to reproduce.  
**Engineering context when originally written:** B5.2 interactive station work (since implemented); for current priorities see the consolidated spec.

## 1. North star

The galaxy is a persistent physical and social world. Players pilot ships directly, explore and survey space, build organizations and infrastructure, and recruit NPCs who make large-scale operations possible. Material is abundant on galactic scales, but finite in each real deposit; economics are determined by accessible supply, human and NPC labor, logistics, maintenance, geography, security, and demand. A resource can move, change form, be repaired, or be recycled; it should not disappear arbitrarily. A successful civilization can endure rather than being scripted to decay.

**World-building principle:** No artificial player XP or ship/system access gates. Player progression is actual ability, discovery, verified achievements, reputation, acquired technology and relationships. NPCs, by contrast, have careers and job experience. Player achievements can attract recruitable NPC specialists, whose labor and knowledge enable automation and organizational growth.

## 2. Decision register — principles explicitly agreed in discussion

| ID | Decision / constraint | Engineering implication |
|---|---|---|
| D01 | Original game, not an EVE clone | Independent code, assets and terminology; EVEmu study only informs architectural choices. |
| D02 | No character skill levels, skill-point grind, or real-time skill queue | Ship use depends on possession, engineering compatibility, player ability; no arbitrary race-specific skill locks. |
| D03 | Manual, Wing Commander-style player piloting; automation optional | Direct movement and aiming plus assists such as orbit, approach, follow, docking and autopilot. |
| D04 | No achievement gates on ship classes or star-system entry | Territory is controlled by physical defenses and player organizations, not a global XP prerequisite. |
| D05 | Achievements should matter materially | Verified achievements improve NPC recruitment opportunities and standing; ranks can rise and fall with performance. |
| D06 | NPCs have backgrounds and agency | NPCs evaluate player/corporation track record, can accept/refuse employment and may later depart. |
| D07 | Base NPC productivity equals an average player under equivalent conditions | Establish a common job throughput baseline before experience, equipment and context modifiers. |
| D08 | NPCs accumulate job-specific experience through actual work | Higher job proficiency can exceed average player throughput; no automatic player XP. |
| D09 | Higher-job-level NPC can mentor lower-job-level NPC | Same-job XP bonus only while mentor level > apprentice level; higher mentor levels give larger bonus; bonus ends at parity. |
| D10 | Infrastructure needs NPC labor to operate | Stations, refineries, gates, shipyards, planetary colonies require staffing, supplies and maintenance. |
| D11 | NPC loss is persistent, not instant respawn | Death/retirement/defection causes shortages; recruitment, migration and training replace expertise over time. |
| D12 | Station condition reflects operational productivity | Understaffing, supply shortages, failed maintenance can cause dark/dirty corridors, slow or unavailable services. |
| D13 | Location-specific storage and physical logistics | Cargo, station hangars, infrastructure inventories and material transfers are distinct, with no teleporting items. |
| D14 | Local deposits are finite, but space has abundant resources | Large geological deposits, no automatic asteroid respawn, meaningful extraction and hauling. |
| D15 | Migration is **not** mandatory or scripted | Relocation/expansion only when economics or operations make a location unsustainable; some settlements can thrive indefinitely. |
| D16 | Repair, salvage and recycling reclaim material | Damage changes component state and recoverability; full wreck mass remains accounted for (including dispersed/unrecoverable fractions). |
| D17 | Discovery data persists | Reentering a system does not erase prior discoveries; records have timestamps, provenance, confidence and freshness. |
| D18 | Surveys contribute to shared/global galactic knowledge | Player credited and possibly paid for verified submissions; multiple independent observations can improve data quality. |

## 3. Layered gameplay loops

**Immediate sortie loop (partially implemented):** docked ship → undock → travel/scan → mine/salvage/fight → accumulate physical cargo → return/dock → unload into the local hangar → refine, repair, manufacture, sell, or reload. B5 currently tests dock/undock, mining and unload headlessly; B5.2 adds live client controls.

**Industrial loop:** ore extraction and salvage → local transportation → refining/recycling → materials and recovered assemblies → manufacturing and repair → equipped ships/modules → combat/operations → wrecks, reusable parts and salvage.

**Organization loop:** verified achievements → NPC applicants/specialists → staffing/mentorship → better infrastructure operation and delegation → new capacity and discoveries → further achievements and NPC relationships.

**Geographical loop:** home station → progressively surveyed systems → establish extraction/transport/maintenance routes → evaluate local viability → preserve, specialize, import, expand or relocate as circumstances dictate.

**Strategic loop:** security and sovereignty shape threats and defense; corporations protect mineral supply, personnel, gates and trade routes; attacks can disrupt logistics, production and ship replacement. Territory value is produced by its resources, location and infrastructure, not merely sovereignty flags.

## 4. Galactic security, navigation and spatial risk

- NPC-controlled safe-start regions, low-enforcement frontiers, NPC-controlled dangerous pockets and player-claimed territories can coexist. **Security, sovereignty and connectivity are independent attributes.**
- Routes have topology. The shortest path might traverse a vulnerable chokepoint; alternate routes cost time and fuel. Gate camps, scanning, escorts and local intelligence matter.
- Docking, undocking, jumping and post-jump protection are validated state transitions; failures and attack windows should be explicit. Manual travel can be faster; long repetitive journeys should not be confused with compelling gameplay.
- Stronger dangerous-region NPC encounters may yield more valuable salvage, materials or rare named equipment; those drops depend on encounter/faction and should not be a simple uniform rarity multiplier.
- Make knowledge of routes and hostile contacts useful while respecting that some information becomes stale.

## 5. Scanning, cartography and the knowledge economy

**Observation model:** distinguish authoritative world truth, sensor observations and a player's or organization's persistent knowledge. Initial entry reveals core astronomy; further directional, probe, geological, tactical, planetary and moon surveys uncover richer data. Deployable probes can require management and retrieval, without repetitive punishment. Ship contacts are transient; the age of a report matters.

**Shared survey network:** store observer, method, approximate coordinates, timestamp, confidence, source and rights to publish. Reward original, useful, corroborated measurements rather than repeated scans. Require independent verification to resist fraud. Aggregate observations with age/uncertainty weighting, without treating repeated identical submissions as new evidence. Public data can coexist with private, delayed or licensed intelligence. Offline-first play requires a later defined synchronization and anti-cheat model; uploading local records must not constitute trusted authority on the server.

## 6. NPC workforce, experience and mentorship

**Baseline:** a competent new NPC and average player using the same equipment have equivalent productive output. NPCs gain task-specific job XP from actual work; accumulated proficiency affects yield, error rates or throughput within equipment constraints. General lifespan does not directly generate idle XP.

**Mentorship rule (agreed):** for job `j`, A can mentor B if `level(A,j) > level(B,j)`. B gains an additional job-XP multiplier while performing qualifying work; higher-level A provides a stronger boost. It automatically stops once B reaches A's level in that job. Proposed formula and caps require balancing; mentorship load, proximity/communications, and number of simultaneous apprentices remain open.

**Roles:** skilled workers and managers make industrial automation possible. Named achievement-reward NPCs may supervise ordinary paid workforce. Workers have persistent existence and records even when not visually rendered. Schedules and aggregated offscreen labor are acceptable; rendered characters can materialize on demand (the joking “magic closet”) without materializing free labor or ignoring location.

**Station health:** do not use only one global score. Track departmental capacities and backlogs: power, life support, sanitation, maintenance, docking, security, refinery, shipyard, medical. Staffing, equipment, supplies, energy and working conditions determine actual throughput. Visible interiors reflect these conditions: lighting, cleanliness, access, facility speed and repair state. Resilience comes from reserves, training, evacuation, and redundancy.

**Losses:** experienced NPC deaths do not immediately produce replacements. Transferable training, migration and recruitment reduce—not eliminate—dependence on single specialists. Avoid abusive permanent-loss loops in offline or unattended play; specify simulation pause/clock rules later.

## 7. Physical economy and sustainability

- Physical inventories are local; no magical cross-station cargo sharing. Ore refinement and manufacturing consume verified inputs and produce explicit outputs/byproducts.
- Minerals common in nature can remain in high demand if ship and module recipes require large quantities. Destruction generates replacement demand, but salvage may recover substantial material.
- A destroyed ship remains an actual wreck/debris field with accounting for repairable components, recyclable scrap, contamination and dispersed mass. Recovery can be below 100%; material is not silently deleted. Destroyed products lose *utility* before their atoms are lost.
- Air/water and other process materials can be efficiently recycled using energy and maintenance; practical leakage and irreversibility remain. Energy/propellant are distinct consumables.
- Asteroids can contain millions of tonnes (or much more) of material. Track ore composition, grade, recoverable fraction and extraction throughput. **Mass (tonnes) and volume (m³) are different units.** Asteroids are not regenerated for convenience.
- Economic viability emerges from finite local stock, consumption, labor, energy, industry capacity, actual transport cost, danger, storage, reuse and market demand. A station with exhausted mines may still flourish through imports or specialization; a rich station might fail from poor logistics.
- Never script a requirement that everyone move outward. The galaxy is abundant; some places endure, some contract, some migrate, and old areas may develop salvage or trade value.

## 8. Historical engineering roadmap (B5-era; no longer current)

| Gate | Scope | Minimum acceptance |
|---|---|---|
| **B5.2 (next)** | Wire dock/undock/unload to interactive browser and authoritative persisted world | Valid command receipts; station state and local hangar visible; rejection and transition cases; no spoofed actor/owner; replay/resync; Linux tests. |
| B5.3 | Loading cargo and two-station logistics/market | Materials conserved across local hangars, cargo and any sale; credit ledger atomic; inter-station movement real. |
| B6 | Geography and survey baseline | Stable system IDs, directional sensor input, persistent discoveries and stale-data handling. |
| B7 | Refining, repair, mass/material ledger | Reprocessing recipe conservation, loss/byproducts, malfunction rollback, salvage/repair inventory. |
| B8 | NPC workers and departmental operations | Worker identity, baseline throughput parity, staffing shortfall, persistent job XP, death/replacement, mentorship differential. |
| Later | System routes/gates; combat/loot; manufacturing; survey network; player-built stations; moon/planet operations; territorial economy | Invariants, performance budgets, provenance, and UX gates before expanding scope. |

These identifiers after B5.2 are **planning labels**, not claims that a fixed implementation schedule has been approved.

## 9. Open questions / design experiments (not yet decided)

1. NPC job XP curve, maximum bonus, mentor capacity and whether learning by teaching also improves the mentor.
2. How many workers are individually persisted versus simulated as workforce cohorts at high scale; whether every noncritical worker can be recruited as a named NPC.
3. Public/private visibility policy for survey records, reward source, fraud prevention and offline-to-global synchronization.
4. Whether player organizations can restrict access to their stations/gates and how that intersects with public safety and emergent sovereignty.
5. Actual ship control/physics, combat targeting and automation interfaces; controller support and optional assists.
6. How much mining, salvage and refining should be physically detailed versus aggregated for smooth long-term simulation.
7. Respawn and evacuation protections for valuable NPCs; fair offline progression and NPC loss policies.
8. Environmental generation rules and the timescales for geological depletion, station consumption and population growth.

## 10. Source and evidence boundary

These ideas derive from the design conversation. EVE examples were discussed as inspiration and are not gameplay requirements. Qwen's submitted *Q2 NPC & Economy Report* and *Q4 Engineering Handoff* support the caution that the examined `evemu_Crucible` revision includes combat NPC logic but no autonomous NPC mining/trading/hauling; Q3 contains **unexecuted** test designs. Historical client-side behavior should not be treated as proven merely because emulator server source exists. Original-game integration should be implemented independently and tested against its own contracts.

**Historical B5-era status (superseded):** B5 source archive included the station lifecycle and Linux tests. For current B7.0b achievements, see the consolidated spec and `SingleGalaxy_B7_0b_Colony_Visual_Geology/docs/B7_0b_Colony_Visual_Geology_Report_2026-10-30.md`. Windows-native execution and actual Chromium visual verification still have not been demonstrated.
