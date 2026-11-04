# COLONY-AUTO-04D.5C1 — Resource-backed NPC habitat resupply

**Implementation:** v80 / November 3, 2026 (America/Phoenix). Parent branch: v79 durable NPC supervisor.

## Motivation

NPC emergency response was capable of noticing an impending life-support crisis, selecting a verified rescue ship and surviving restarts. However, the colony could not yet autonomously use ordinary local supplies to avert a preventable evacuation. We should use the already authoritative game operations instead of a parallel “free NPC repair” economy.

## Delivered vertical slice

`PersistentWorld::supervise_npc_habitat_resupply(source,site,command_id)` is a trusted host loop callback. It accepts only an occupied player-owned physical **habitat** connected to its actual local colony ship, within existing docking range and the same solar system. An actual `colony_safety::assess` must be **Critical** or **Emergency**; safe/forecast-only sites do not trigger emergency dispatch. The site must have a current **committed** shortage threshold event with a physically present, duty-eligible living NPC in its `sg_colony_npc_opportunities` roster. The host performs real `acknowledge_colony_notification`; an observation opportunity alone does not assert that the NPC knows or acted.

The dispatcher sorts food/water/oxygen by remaining real consumption runway, then chooses an available onboard resource where the habitat has fewer than four full consumption periods. It transfers only the physical deficit or the smaller available onboard amount, capped by the native 10,000 kg envelope. It does not reserve negative stock, move between distant ships, or conjure shipments. No hostile/nonowner ship can supply a habitat through this pathway.

The action is the existing native `Action::TransferHabitatStock`, admitted as the actual player-owner-authorized command (host-supervised, not an unrestricted new `kServerAuthority` action). The new immutable `sg_colony_npc_emergency_supply` receipt and the accepted command are persisted in **one canonical `Store::commit`** with the NPC identity, physically delivered warning, original source/destination stock and actual shortage. A separate subsequent ordinary `PersistentWorld::step()` executes the existing game operation and moves the kilograms. The dispatcher explicitly returns **Accepted, not Delivered**; `verified_npc_habitat_resupply` returns true only if the command was actually Applied and the transfer amount reconciles.

`Store::verify_npc_habitat_supplies` independently replays actual source and destination from the pinned admission snapshot, cross-checks the command `operation_log`, current at-admission physical shortage and real local NPC, delivered notice and correct original event, signed amount, native historical Applied event, world resource conservation, and replayed post-execution snapshot. It refuses corrupt or omitted evidence at startup and independent `recover_copy`. Exactly the original accepted and completed snapshot generations are exempt from checkpoint pruning, so an old legitimate delivery can be audited after many world ticks. Failure injection rolls back accepted command and new receipt together; a delivered notice can be retried idempotently.

This does **not** replace existing resource-backed emergency utility maintenance from `src/Simulation.cpp`: once a stationed utility has a real habitat emergency, local technician(s), pre-delivered modules, and enough life-support service, native `process_utilities` can repair actual damage while consuming a module, even when the owner disabled optional standing delegated maintenance. Existing `COLONY_AUTO04D3_PhysicalRepairTests.cpp` remains part of focused CI.

## Explicit limits and next sub-gates

1. **04D.5C2 cross-site/cross-ship supply transport** needs an authenticated carrying entity, distance/travel, loading, unloading, custody journal, capacity, fuel/energy and delivery failure. The v80 callback only transfers ship-held goods into an actual nearby *owned* habitat. It does not deliver supplies from rescue ships or distant regions.
2. **04D.5C3 durable repair and distress work orders** should bind real NPC availability, site spares, utility or hull damage, and actual radio delivery; a repair/distress intent is not a completed repair/transmission.
3. **04D.5C4 automation scheduler integration** must invoke this host callback at the correct boundary and coordinate with the v79 family evacuation plan to prevent competing same-tick commands. Avoid running stock transfer on a tick when a full-family rescue has already been admitted.
4. This dispatcher needs an actual locally available responder. It never creates population to supply an empty, abandoned or unstaffed habitat. Adult-only colony evacuation is still a separate regional-authority gate.

## Acceptance

Native CTest `galaxy_colony_auto04d5c1_npc_resupply` uses real colony construction, mined/refined/manufactured material, a constructed habitat and three physically assigned named residents. It verifies no order while safe, a real local food Critical threshold, an NPC alert and delivery, a genuine 8-kg resupply order from finite ship inventory, admission and execution faultpoint rollback, restart before world execution, actual Applied transfer, unchanged total material mass, preservation after many world ticks and `recover_copy`, and refusal of privileged tampering even with the immutable trigger restored.

The dedicated GitHub Actions workflow also runs v79 durable emergency supervisor and prior physical utility-repair acceptance. **Focused acceptance PASSED:** [GitHub Actions run 41776373351](https://github.com/vjmichael/Single-galaxy/actions/runs/41776373351), executable commit `cedae6e00165f4079d2c0ea50503e092ab46cfb0`; all 3/3 actual native CTests passed: physical utility repair, durable NPC family supervisor and NPC local resupply. Full selected test duration 1.87 seconds. Subsequent commits documenting defenses do not change executable C++.
