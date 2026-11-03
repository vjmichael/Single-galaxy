# COLONY-AUTO-04 — Life-support autonomy, colonist exit, and accountable governance

**Recorded:** 2026-11-03. **Status:** Approved design direction / implementation acceptance proposal, **NOT yet implemented or passed in the game runtime**. Applies to the original Single-Galaxy simulation. Complements COLONY-AUTO-01 (per-operation delegation and specialized production), COLONY-AUTO-02 (externalized life-support service agreements), and COLONY-AUTO-03 (NPC administrator succession). Preserve existing physical-person, cargo, transport, warehouse and cold/detailed reconciliation rules.

## Foundational invariants

1. **A production switch is not a consumption switch.** A player can disable local food, water, oxygen production, power generation, waste processing and maintenance when they choose, including fully specializing colonies. The existing population continues to consume and physically needs the services, whether locally generated, imported, piped through constructed infrastructure or drawn from finite reserves. No automatic goods are generated.
2. **The player controls strategic policy, not NPC self-preservation.** No standing policy, labor command, administrator appointment or transport order can prevent competent NPCs from attempting escape, rescue or urgent maintenance when a dangerous shortage is credible. Under imminent hazard, emergency work/refusal/evacuation can override lower-priority player production directives, with a recorded reason and bounded scope. This is not a magic resource or instant transfer.
3. **Do not enforce a lethal trap.** An inhabited occupied life-support compartment cannot be instantly deprived of the last breathable atmosphere by a discretionary direct player UI action without viable protective action; fail closed on direct lethal sabotage, and remember the attempt. Disabling future production remains a real and permitted economic decision, but cannot compel inhabitants to stand idle as systems fail. Emergency controls, alternative exits, communication and transport requests remain independently operable by the colonists where physically functional. No ladder-removal-type imprisonment while a hazard is developing.
4. **Risk must have warnings and consequences.** A life-support service shutdown without an actually committed alternative will warn, provide finite-stock/endurance projections and permit risky economic specialization; it triggers autonomous attempts to obtain supplies, reassign labor or depart. Player policy cannot silence critical condition reporting.
5. **Physical conservation always wins.** Migrants, cargo, rescue vessels, energy, materials and berths must exist and be represented in the authoritative simulation. No NPC is teleported or duplicated; no free rescue ship, stocks, invented dependents, virtual escape route or hidden casualty correction. Stranded people remain durably present and eligible for subsequent rescue rather than being silently removed.
6. **Grievance versus danger are distinct.** The social threshold for voluntary protest and emigration is NPC-specific and may take time. The physical danger threshold for immediate emergency protection or evacuation is not delayed by an NPC's loyalty, patience or grievance score. Supply forecasts trigger protective preparation **before** food, water and oxygen hit zero. At actual exhaustion, all eligible actors take urgent life-preserving actions, not routine work.
7. **Independent, durable, nonuniversal memory.** Authoritative ledger events record exactly which policy/command disabled a service, player or delegate identity, warning acknowledged, alternative supply claimed/verified, delivery missed, and physical consequences. APS/GRSM distributes eyewitness reports, evidence and rumors with different interpretations, not a universal morality meter or omniscient player accusation.
8. **Dependencies and protected people.** Existing explicit nonfatal dependent transport gate and non-graphic dependent presentation policy remain binding. No child injury/death visuals or selectable child-harm commands. Preserve parent/guardian custody, explicit receiving guardians and actual transport capacity. Decline any fatal-dependent rescue/transport transition not supported by the durable custody gate.
9. **No generalized police/EMS invention.** Emergency response comes from available NPC workers, managers, ships, infrastructure and legitimate mutual-aid agreements; police/fire/full institutional EMS are still deferred.
10. **Keep player-founded colony ownership separate from operational obedience.** Founders retain strategic/title authority under normal gameplay. NPCs retain the ability to refuse dangerous instructions, abandon posts, elect caretakers on absence/death, form labor groups and seek refuge. If a player deliberately strands people, the surviving community may cease following the player's directives; formal political independence is a later, explicit governance gate, not a silent transfer of world title.

## The two parallel colony-state machines

**Physical essential-services state (per resource and combined):** Stable -> Forecast Shortfall -> Critical Buffer -> Emergency -> Evacuating / Sheltering / Under Rescue -> Recovered or Uninhabited/Stranded. Transitions are based on measured local inventories, real rate of consumption, reserve accessibility, infrastructure condition, expected in-flight receipts and service dependency graph. Forecast credibility matters: a proposed but uncommitted delivery does not count as stocked inventory. Do not use a single arbitrary zero-stock trigger to begin departure. Return from emergency requires actually restored service or confirmed safe occupancy, not merely selecting a UI checkbox.

**Social legitimacy state (per NPC/cohort and group):** Content -> Concerned -> Aggrieved -> Coordinated Action -> Refusal/Strike/Exit -> Later Reassessment. Severity depends on hazards, evidence of repeated neglect vs malicious intent, available alternatives, personal circumstances, historical promises, family/community support, and managerial response. These thresholds affect how early people voluntarily leave or organize; they cannot suppress emergency survival action.

A shortage caused by a naturally destroyed freighter is distinguished from a deliberate shutdown after warnings. An NPC can be mistaken about motive; authoritative event records should not infer intent from resource shortage alone. Witnesses may debate what the player knew and did.

## Emergency coordinator and policy hierarchy

1. First preserve lives and safe escape paths; actual breathable enclosure, shelter and evacuation controls take precedence over discretionary factories and exports.
2. Inventory and generation-scoped world ownership remain physical truth. Preserve finite essential-service stocks and agreed emergency buffers when available. Emergency consumption and rationing must use the same physical debits as normal operations.
3. Temporarily reassign qualified workforce to repairs, recycling, air/water processing, ration distribution and transport coordination, with health and competency limits. Reassignment displaces real productive work.
4. Notify player, administrator and available colonies with grounded remaining-endurance forecasts, provenance and required transport volumes. Request mutual aid through real inter-colony service agreements.
5. Execute ordered physical departures and rescue transport as resources and berths permit; choose receiving places with verified shelter/capacity and guardian custody. If no transport is available, people shelter, repair, build or await rescue as actually feasible, retaining exact stranding records.
6. Record every emergency override, decision, resource transaction, active voyage, failed attempt, arrival and resulting population delta in the authoritative journal; reconciliation must be idempotent across restart and cold/detailed transitions.
7. Player can intervene with a physically viable remedy or reverse harmful policy; cannot cancel emergency self-preservation by revoking an NPC manager's authority.

## Proposed service policy data (conceptual, not schema)

`colony_id`, `service_kind`, `local_production_enabled`, `external_supply_contract_ids`, `minimum_stock`, `emergency_reserve_target`, `assigned_worker_priority`, `delegated_planner_authority`, `last_player_policy_command`, `last_forecast`, `last_verified_supply`, `state_generation`.

Work-order and planning adapters must distinguish a *scheduled/expected* future arrival from verified committed warehouse receipts. An external life-support provider can fail without magically changing the consuming colony's state.

## Player-facing outcomes and memory

- Prior to a shutdown: concise notice identifying which life support is now externally dependent, actual current reserves, the next **verified** shipment or pipeline, a realistic range for buffer endurance, and missing capacity. If unknown, state *unknown* rather than invent hours.
- During prolonged shortage: staged reports about labor reallocations, warnings, complaints, refusals, available relocation opportunities and actual departures. Provide a compact colony-readiness / evacuation view, not mandatory individual micromanagement.
- During immediate hazard: a non-suppressible critical alert and automatically entered emergency mode; direct lethal trap commands fail and create an attempted-exposure event for accountability.
- After evacuation/recovery: the NPC and society records persist. Direct witnesses may remember warnings ignored, emergency assistance blocked, escape attempts, a rescue organized, or an abusive order. Rumors can distort facts; trust and cooperation can recover gradually with evidence of changed behavior, but cannot be globally reset by a UI action.
- Leadership succession: if a player-appointed administrator dies or cannot serve, a limited caretaker starts, the player receives an appointment period (duration configurable in active simulation time), then NPCs elect a successor if the player doesn't appoint one. No free NPC creation; ongoing emergency response cannot wait for this election.

## Minimum future acceptance suite

1. Disable local food/water/oxygen supply with real signed import contracts: consumption continues; no phantom imports; physical receipts replenish actual inventory.
2. Cancel or miss imports: forecast shortfall before exhaustion, player notice, reassigned NPC labor, community complaint and viable physical departures; no silent auto-healing.
3. Attempt the 'pool ladder' equivalent: deny forced entrapment or instantaneous lethal atmosphere cutoff in inhabited locations, preserve access to emergency systems, and audit the attempted order. The player can still decommission uninhabited structures under ordinary authority.
4. Low grievance NPC remains loyal while still participating in emergency life-preservation; high grievance NPC may leave earlier when a real transport/receiving capacity exists. Separate social from safety clocks.
5. Repeated and evidence-backed player sabotage produces durable per-witness and group memories, refusal, migration and long-term trust loss; accidental freighter loss should not automatically receive the same intent inference.
6. One ship capacity-limited evacuation and multi-vessel evacuation: check every named adult/dependent once; no impossible teleport, duplication or missing-guardian cases. Atomic rollback on injection at source debit, passenger boarding, journey and destination receipt.
7. Cold -> detailed -> cold and SQLite restart with active shortage, admin vacancy, ongoing transport and a partially completed rescue: no repeated consumption, double migration, phantom workers or overwritten immutable evidence.
8. No escape craft or receiving site: no invented craft or disappearance; retain named/aggregate stranded survivors in authoritative world state with rescue requests and bounded emergency action.
9. Provider colony runs out of goods; consumer cannot disable its own consumption to hide the failure, and provider cannot satisfy promises with unfinished factory output.
10. Administrator death mid-emergency: durable caretaker continuity, eventual appointment/election, preserved warnings/contracts/resources, unchanged emergency protection priority.
11. Player re-enables local production: emergency stand-down only when real supplies, environmental conditions and verified receiving capacity justify it, not on a policy flag alone.
12. Independent verifier reconstructs resource quantities, transport ID exclusivity, origin/destination population, source policy/evidence and cold-mode generation fences.

## Delivery boundary

This file documents intended colony gameplay, migration/social response and safety constraints. **It does not implement** the delegation planner, resource service agreements, evacuation state machine, direct-sabotage block, real rescue vehicles, NPC elections or GRSM propagation. Implementation must be separately tested before the gate is closed. The v64 game-host integration is a prerequisite foundation but is not evidence that these rules already execute.
