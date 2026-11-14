# COLONY-AUTO-04D.4C — Atomic Bounded Adult Colony Departure

Date: November 3, 2026. Branch: `integration/colony-auto04d4c-atomic-departure-v72-20261103`. Base v71, which introduced versioned physical NPC consumption.

## Authority and semantic contract

**A colony is not evacuated because a plan said so.** The source becomes an evacuated physical hull only after an applied, authoritative, single-world-command relocation of **every living named adult** belonging to that colony, plus source and receiving crew/workforce reconciliation.

The new `Action::HostColonyDeparture` is host-only, never a player command. Every departure carries its exact, canonical, sorted, unique NPC ID manifest (maximum 16 in this one-receiver slice). `PersistentWorld::submit_host_colony_departure` queues it under the exclusive world writer; `step_with_colony_departure` is the **only** allowed execution path, checking exactly one due host command and revalidating the complete manifest and resources immediately before the physical step. Ordinary `step()` refuses the queued action.

The atomic world command:

- Requires a *critical or emergency* source ship or occupied source facility; an ordinary healthy colony cannot be stripped by this emergency API.
- Requires source and receiver to be real, intact, **same-owner**, nearby ships in the same system, ready and physically in space. Cross-owner physical custody requires a verified independent host acceptance/receiving authority and is refused in this slice. Source outpost occupants must be physically near the receiver too.
- Requires the complete current named population, including original operating crew, farm workers, and habitat residents, with no duplicate, missing, offsite, or already-transported IDs.
- Requires a distinct real receiver with its own surviving operating crew and **actual** food, water and oxygen sufficient for at least two consumption periods for everyone already present plus all relocating people.
- Refuses unresolved active passenger voyages, **any recorded ART02 dependent voyage/child custody link** at source, receiver or source outposts, **any canonical society birth record** tied to the source or receiving ship, queued competing orders, and staffed specialty/clinical/service obligations needing separate settlement. This intentionally also refuses conservative historical family links until lawful closure can be proven.
- Transfers all residents to the receiving ship, sets their permanent crew assignment and location to that ship, increments receiving crew, zeros original ship/farm/habitat headcounts, and emits an applied world event. Never destroys a real person.
- Leaves all physical cargo, food, water, oxygen, buildings and the source hull in their original locations. The original ship is **intact, evacuated, and permanently non-operational**, not a wreck. The evacuated-hull registry prevents further normal commands from moving it.
- Records the complete immutable roster, source, destination, command ID and tick in the canonical **v58 world snapshot**, and cross-checks it against the applied event (with material transferred=0) and individual living NPC custody on reload and independent journal replay.
- Uses the same SQLite `BEGIN IMMEDIATE` world transaction and single-generation commit as other world effects. A crash/failpoint before commit publishes neither the departure nor a false completed rescue.

### Explicit non-goals and hard safety boundaries

This is **not yet universal full-colony evacuation**. It is bounded to an all-adult, one-receiver, near-range 16-person manifest with no active dependents or unresolved clinical/social work. Acknowledged dependent guardianship, physically routed multi-ferry departures, household-level ART02 custody, regional cold/warm acceptance, and ongoing shipborne supply escrow are separate gates. Children must never be silently moved by a generic adult manifest; unresolved family scopes must cause refusal until the integrated dependent authority can attest them.

This version physically transfers people to a receiving ship at docking range; it does **not** model long-range rescue voyages, fuel, road/ship transports between remote outposts, or certify offsite households as evacuated. Emergency host authority cannot forcibly take over another player's ship. No player veto may suppress the life-support assessment, but that safety priority never authorizes spawning resources, fabricating transport or declaring an impossible rescue complete.

### Tests and evidence

`tests/COLONY_AUTO04D4C_AtomicDepartureTests.cpp` checks healthy-source refusal, incomplete/duplicate/forged/underfunded refusal, physical four-NPC whole-crew transfer, source hull intact and inaccessible, final exact custody and receiver eight-person needs, non-duplicated original resources, failpoint rollback after snapshot write, restart recovery, strict v58 snapshot parse and independent `PersistentWorld::recover_copy` replay.

`.github/workflows/colony-auto04d4c.yml` runs the new departure CTest alongside predecessor life-safety, authenticated relay, durable journal, NPC notification, repair, passenger rescue, resource custody and manifest survey tests. The branch is a **draft** until CI acceptance actually completes.
