# NPC-RESP-03A: Criminal, territorial and militant NPC colonies

A settlement's social history, leadership agenda, diplomacy, physical territorial claims and published visitor policy are different sources of intent. The existing `ColonyConflict` agenda/relation model and `ColonyDefense` actual sightings remain authoritative inputs for decisions. This addition introduces `ColonyThreatPolicy.hpp`, an advisory projection of how a colony **wants** to respond to a foreign ship.

| Colony type | Uninvited visitor | Invited visitor | Expelled returnee |
| --- | --- | --- | --- |
| Cooperative | Ignore or warn if already hostile | Normal access | May warn under unfavorable relations |
| Territorial | Hail / warn to leave a verified region | Normally permitted | Intercept within exclusion policy |
| Criminal | Shadow, extort or prepare ambush in claimed area | Normally allowed, despite predatory character | May stage an ambush |
| Militant | Immediate interception intent in claimed region | Normally permitted | Interception intent |

An existing `Agenda::Predatory` can feed a criminal archetype, but a criminal NPC **colony is not a crime verdict against the player**. Illegal raiders can intend an attack without a legal warrant, but attacks still require native ship weapons, ammunition, target acquisition, jurisdictional event receipts, consequence/reputation accounting and persistent NPC identity. Territory may be claimed by settlers, disputed by other colonies, or unknown to a visiting player; a visitor can inadvertently enter it.

**Important:** `VisitorStanding::Uninvited` is a separate fact from merely detecting a foreign ship. Claims, invitations, warnings, deadlines, boundary crossings, nonaggression agreements and expulsion notices will need original durable receipts. Until authenticated receipts exist, the threat evaluator is advisory only, never a command authority. Player-owned colonies can use the same policy machinery, subject to identical resources and evidence.

For NPC-RESP-03 the following ledgers must be implemented next (no invented prerequisites):
1. Region claim and verifiable local entry/exit observations tied to the existing single-source physical geography.
2. Owner/faction-controlled invitations, visitor access lists, warnings with expiration/grace, blockade notices and treaty exceptions.
3. Distinct incident types: uninvited visit, trespass after warning, theft, attack, piracy and distress. A hostile *opinion* is not verified misconduct.
4. Native intent dispatch: use existing qualified physical NPC pilot and `LocalRendezvous` to hail, shadow or pursue. Hostile attacks must be dispatched separately through real combat authority.
5. Real disabling weapons and persistent hull damage, with reason/munition receipts and recovery. Emergency rescue and boarding retain their original crew/person authority.
6. Player capture/arrest/detention: lawful police custody uses warrant/jurisdiction; unlawful pirate capture has a **different** coercive-custody reason and downstream escape/ransom/reputation consequences. Both require a physically embodied player, not ship owner reassignment.
7. All identities and actions survive crashes, replay and restart without duplicating prisoners, NPC crews, loot or resources.

Native `galaxy_npcresp03_threat_policy` covers visitor states, territorial exclusion, criminal ambush intent, militant intercept and hostile diplomacy; enumerates cases to prove no advisory intent becomes an arrest, goods seizure or damage permit. Existing FLIGHT-03/04 and NPC-RESP-01/02B remain the movement/physical custody foundations.

**Status:** Source and tests committed; native Windows/Linux tests not yet accepted. No combat, coercive capture, faction warrants or completed territorial incident ledger is claimed.
