# Single Galaxy DEMOG-05a–05e record and DEMOG-05f integration gate (2026-11-02)

## Completed local prototypes (not integrated into main game)
- **05a:** saved-world NPC identity baseline, 24 NPC fixture, reopen.
- **05b:** boarding reservations refer to actual PersistentWorld ship/NPC IDs; SQLite uniqueness rejects competing bookings.
- **05c:** durable reserved/departed/arrived voyage state and event journal, replay rejection.
- **05d:** transactional cancellation, archived manifest, seat release, rebooking.
- **05e:** host-certified seat limit; atomic seat counting and reservation, overbooking rejected. Seat rating not yet derived from physical cabins.
- Prior DEMOG-04b3–04b6 tested synthetic cold/warm travel and integrity, including 12,000 travelers; these are NOT production-world transport tests.

## Source audit for 05f
`PersistentWorld::world()` returns **const Simulation&**. `Simulation::npc_records()` returns **const std::map<uint64_t,NpcRecord>&**. `Simulation::move_npc_records` is not an exposed public passenger-transfer command. Current `WorldBoarding` uses separate extension tables in the world SQLite file and merely records voyages. It cannot legitimately update NPC location, world aggregate headcounts, family and faction state through public authoritative interfaces. In particular, a voyage marked `arrived` does **not** mean an NPC physically arrived.

## Required 05f authoritative implementation
1. Add a trusted `PersistentWorld` command to embark/disembark a manifest, with stable command ID, world generation/ownership checks, ship capacity from physical components, source and destination validation.
2. Apply NPC location and colony/ship headcounts through the same authoritative simulation mutation, persist snapshot/receipt and durable event atomically in the world transaction. Do not independently mutate prototype ledger and game state.
3. Reject stale or conflicting reservations, duplicate NPC IDs, destroyed ships, invalid destination, missing family/identity records, and over-capacity.
4. Resume from an interrupted save/restart using the same command ID and receipt; exactly-once NPC custody effects, idempotent replay.
5. Ensure all faction rosters are projections from canonical NPC IDs and revisions. Push events for UI but do not authorize from stale projections.
6. Verify named NPCs are not counted twice in aggregate cohorts, and preserve all genealogy/skill/employment references.
7. Test cancellation before embark, cancellation after embark, destruction, arrival, repeat arrival, cross-region cold/warm, and concurrent booking.

## Acceptance
A test must save a real world, embark Bob from an actual site onto a ship, reopen, travel, disembark at another site, reopen, and compare his identity/family/job/skills/faction references byte-for-byte while population and capacity reconcile. **Do not mark 05f complete until that passes.**

## Game-design note
Passenger transport is a potential emergent profession: player-operated scheduled liners, shuttles, migration charters, emergency evacuation and crew transport. It uses physical ships, actual people and contracts; not a detached teleport/minigame.
