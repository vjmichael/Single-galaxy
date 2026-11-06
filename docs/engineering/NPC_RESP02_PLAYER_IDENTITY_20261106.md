# NPC-RESP-02 — Persistent player persona independent of vessel

**Native source committed; Windows/Linux CI not yet accepted.**

## Authority and identity

The player character is now a distinct `PlayerPerson` world record, keyed by the original `PlayerId`, with a durable physical ship, original home ship, explicit custodian and state, and timestamp/receipt slots. It is **not** a named NPC passenger, hull ownership, or just `FleetSelection::piloted()`. V98 introduces `playerperson` canonical snapshot rows, roundtrips prior snapshot formats and validates the owner/physical ship pairing after restore.

The only initial transition is `register_player_person`, committed through the original `PersistentWorld::register_player_person` copied-state SQLite transaction. It requires the registered player to own a real, not-in-transition physical ship. It rejects wrong owner, duplicate characters and forged locations. It does NOT automatically move, rescue or detain anyone. An explicitly registered person is counted exactly once in the existing `people_physically_aboard` read model and therefore in native colony life-support needs for opt-in passenger-needs worlds.

## Acceptance
`galaxy_npcresp02_player_identity` verifies wrong-owner and foreign-ship registration refused, SQLite failed publication leaves canonical world unchanged, one durable player character persists across restart, v98 snapshot roundtrip and owner-to-foreign-ship tamper rejection. Windows and Linux native CI includes the original NPC response and dependent-family rescue regressions.

## Required later independent physics/authority
- Rescue: verifiable player-owned distress consent, original qualified physical rescue ship, a completed native FLIGHT-03 rendezvous and real receiving ship capacity. After authorization, a durable player-person transit should update the physical vessel and continue existing finite ration accounting; cannot be a change to vessel ownership.
- Enforcement: explicit native observed offense evidence plus a faction/jurisdiction warrant. Contact or subjective `ColonyConflict::Attitude` alone proves no crime.
- Disabling: separate real ship combat/propulsion damage, ammo, collision and incident receipts. Current training weapons cannot hit civilian player vessels.
- Arrest and custody: original lawful warrant, actual immobilized vessel, boarding/custody permission and durable prisoner transfer/release. A ship may be destroyed without implying that its pilot dies or was arrested. Never overwrite named NPC child guardian manifests.
These transitions are **not implemented** in NPC-RESP-02 and must not be claimed accepted.

## Version gates
v98 schema is introduced ONLY after an explicitly authorized player persona registration; v97 and earlier saves without that record remain byte-identical and readable. The world owns the person; UI fleet selection continues to be a non-authoritative control-plane view.
