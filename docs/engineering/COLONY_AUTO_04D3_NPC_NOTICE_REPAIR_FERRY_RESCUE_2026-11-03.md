# COLONY-AUTO-04D.3 — NPC emergency notice outbox and physical response

Date: 2026-11-03. Branch: `integration/colony-auto04d3-observations-v70-20261103`. Base: verified AUTO-04D.2/v69.

## Native notification authority

`ColonySafetyNotice` publishes a distinct **local observation opportunity** for every named NPC whose authoritative `NpcRecord::location` equals the compartment where an actual committed `sg_colony_response_events` shortage threshold occurred. These records are created **inside the same transaction** as the world snapshot and event, on the single `PersistentWorld::Store` SQLite writer.

No named NPC, no opportunity. Offsite NPC, no opportunity. Existing passenger physically aboard the affected site can be eligible; merely sharing an employer or home ship does not make them a witness. The event is **not** an attributed player crime, NPC belief, or evidence of having actually received the alert.

`sg_colony_npc_opportunities` is append-only with primary key `(event_id,npc_id)` and an immutable source event FK. `sg_colony_npc_acks` stores explicit host-attested delivery of exactly one eligible notice. An acknowledger must be a currently living named NPC, and duplicate delivery is idempotent. The API has no HTTP/UI exposure and requires a trusted local host; a player-supplied arbitrary NPC ID must never be passed directly. Delivery does not automatically mutate GRSM reputation or social ties.

`sg_colony_npc_notice_meta` anchors the current world generation and opportunity/acknowledgment counts. Startup verifies the rows, parent world event provenance, monotonic acknowledgment generation/tick, and count invariants. These are **noncryptographic** count/provenance protections, not third-party signatures or universally independently verifiable memories.

## Actual resource-backed emergency repair

In `Simulation::process_utilities()`, a critical/emergency *occupied habitat* may independently initiate repairs **only** when the local utility is sufficiently damaged, existing housed and currently sustained technicians are assigned, and at least one player-manufactured module was physically delivered into the site. This overrides disabled **routine** maintenance delegation, not the requirement to have real labor and spare material.

The existing simulation repair operation deducts one real spare module, increases embodied structural ore-equivalent by two, increments authoritative repair count, and preserves world material mass and people. The legacy `delegated_repairs` counter increments only for regular delegated work; an emergency repair increments total `repairs` but does not falsely claim player permission. A shortage forecast, zero spares or unavailable technicians cannot create a repair.

This is a bounded, local repair mechanism, **not universal independent maintenance or supply delivery**.

## Actual passenger-only emergency ferry rescue

`submit_host_emergency_ferry_rescue` is a narrowly scoped **trusted-host-only admission** for `Action::HostEvacuatePassengers` of a previously booked active/stranded native passenger voyage. The source ferry must be in `Critical` or `Emergency` life-support risk. The exact full named passenger roster must match the authoritative voyage record. The receiving rescue ferry must be a real intact colony ship with native crew, and carry actual current food, water, and oxygen sufficient for at least two nominal consumption periods for native crew, existing passengers and incoming passengers. The existing `submit_host_passenger_action` enforces native berth capacity, vessel proximity, reserved voyage IDs, and dependent-child berth restrictions.

A **successful admission is not a rescue**. The passengers remain aboard the source ferry until the queued native command actually executes. A successful committed `Outcome::Applied` yields a real rescue-voyage identity and relocates named passenger custody. Restart must reproduce those physical passenger and voyage records.

This narrow path does **not** evacuate the source ferry's operating crew, whole colonies, outpost residents, or guardians with dependents. It does not assert that rescue ferry supplies are already debited for passenger travel, nor does it model fuel/routing beyond native current-local proximity. Therefore it is *not* production-ready autonomous colony evacuation. An existing source ferry's native crew can remain endangered after its passengers have been rescued; caller must never display the compartment as safe.

## Native acceptance

- `COLONY_AUTO04D3_NpcNoticeTests.cpp`: 4 physically present founding NPCs, three real shortage transitions, 12 durable opportunities, no automatic acks, host acknowledgment, duplicate retry refusal, forged recipient refusal, crash rollback, restart recovery and tamper refusal.
- `COLONY_AUTO04D3_PhysicalRepairTests.cpp`: initial owner maintenance delegation disabled; actual finite utility spares + onsite technicians + critical state; module consumed, installed mass conserved, repaired condition and persistent state after restart.
- `COLONY_AUTO04D3_FerryRescueTests.cpp`: real two-ship world, named passenger voyage, refusal while source healthy, forged/incomplete manifest denial, critical shortage, stocked receiver, actual execution and restart preserving disjoint custody.

`.github/workflows/colony-auto04d3.yml` builds native tests and checks predecessor v69 journal, authenticated relay, and delegated maintenance regression. **A successful commit or PR is not acceptance**; close only after direct CI evidence.

## Unfinished integration / next gates

1. Bind verified host notification ACK to an individually observed GRSM knowledge receipt, preserving epistemic distinction between notification delivery, belief, hearsay and evaluation.
2. Guarantee real consumption/cooldowns for ferry passengers, explicit voyage life-support escrow, and physically funded rescue/hauling logistics before expanding passenger rescue.
3. Integrate actual dependent manifests and legal guardianship, all-or-nothing group rescue and operating crew rehoming; do not split children or duplicate any survivor.
4. Provide physically routed and debited **inter-colony supply** and evacuation with emergency independent command authority (not player veto), with failure modes where colonists can remain stranded.
5. Verify cold/detailed mode generation and resource reconciliation and Windows acceptance, plus broaden full suite beyond focused CI.
