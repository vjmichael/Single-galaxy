# NPC-RESP-02B — Owner-consented physical player rescue

**Implementation source committed; CI not yet accepted.**

## Character is not ship
NPC-RESP-02/v98 already introduced durable `PlayerPerson` and genuine finite occupant counts. v99 now separates player *consent* from *physical movement*:

1. The authenticated owner of an onboard player persona calls `PersistentWorld::grant_player_rescue_consent(owner,receiving_ship,consent_id)`. This original SQLite transaction grants authority to **one specific real rescue ship**, never all NPCs. No person or ship moves.
2. The NPC response coordinator uses the original qualified NPC pilot, `NpcFlightPurpose::Rescue`, and native moving-target `LocalRendezvous` ship flight. The original applied ship command and `ActivityKind::NpcRendezvous` receipt prove actual approach and matched velocity.
3. `PersistentWorld::complete_player_rescue(owner,receiving_ship,flight_id,transfer_receipt)` **independently** checks the authenticated player's pending consent, actual flight owner/ship/source/purpose, current physical proximity, a new one-time transfer receipt, and the receiving colony ship's free physical berths and two-ration food/water/oxygen capacity.
4. The original copied-state SQLite commit changes only the durable `PlayerPerson::physical_ship` and rescue state/custodian; the player's original hull stays intact and retains ownership. An original `ActivityKind::PlayerRescued` event binds the separate person-movement receipt to the original source and receiver.
5. Both ship and person locations derive from original game entities, and `people_physically_aboard` counts the player exactly once, so every later native colony support cycle consumes exactly one additional real person's food, water and oxygen. No phantom goods or clone NPCs.
6. Canonical v99 snapshot parser independently checks the original authorized rescue flight, physical ship owner, exact activity receipt, consent ID, and one-person body provenance. Previous v98 and older snapshots remain readable.

This is a **player rescue movement**, not an arrest, permanent confiscation, cockpit handoff or detention. There is no permission to pilot the rescuing NPC ship. Changing `FleetSelection` and physically travelling back to a player-owned hull require explicit later gameplay transitions.

## Native acceptance
`galaxy_npcresp02b_player_rescue` verifies owner spoofing, consent to own hull denial, faulted consent transaction, native unstarted flight denying transfer, real approaching NPC and original receipt, faulted SQLite rescue denying transfer, separate transferred-person event, one occupant moving from origin to receiver, no ship ownership mutation, finite life-support consumption, conservation of world matter, v99 snapshot roundtrip, forged person transfer receipt rejected and restart retaining custody. Windows/Linux CI includes preceding v98 and NPC-RESP-01 regressions.

## Outstanding law and combat gates
Actual disable/attack rules, incident evidence, arrest warrants, force escalation, being captured without consent, dedicated escape pods and safe release remain **not implemented**. An observation or diplomatic dislike grants no warrant. Current `FireTrainingWeapon` is strictly training-drone-only and must not be reused as police damage. All such authority must be proven independently before arrest or disabling can be activated.

## Native control authority after physical rescue
`FleetSelection::is_ready` now refuses cockpit selection for a registered person unless the player remains physically aboard that exact owned vessel in `AboardOwnShip` state. The original player-side `PersistentWorld::submit` gate additionally denies direct Thrust, Dock/Undock, Jump, manual Mine/StartMining/StopMining, training-weapon fire and direct SalvageWreck from a different ship or while rescued. Original **NPC** flight controls use the preexisting trusted `Simulation::submit` and remain unaffected. This intentionally prevents physical teleportation through the GUI; future owning-ship transfers require independently authorized boarding.

The positive native test now verifies the player cannot thrust or manually start mining the abandoned source ship once aboard their rescuer.

## Revocable permission
The owner can now explicitly withdraw unconsumed consent through `PersistentWorld::revoke_player_rescue_consent`. It is committed as a distinct original SQLite transaction and clears the particular pending authorization without moving the person or canceling another ship's original navigation. Attempted completion without a current consent fails even if an NPC rendezvous later finishes. The native test exercises wrong-owner revocation, successful revocation, nonrepeatability and a fresh consent before successful rescue.
