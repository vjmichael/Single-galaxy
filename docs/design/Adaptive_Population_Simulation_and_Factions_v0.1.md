# Adaptive Population Simulation (APS) and Emergent Factions

**Single Galaxy design notes v0.1 — 2026-10-31**  
**Status:** approved architecture direction; only a small opt-in individual workforce and a standalone cohort arithmetic prototype are implemented in B7.0n.

## Hierarchical simulation

Galaxy / region → colony → cohorts and factions → workgroups and households → selected detailed NPCs.

Do **not** update the entire galaxy, every colony or every individual on each simulation tick. Represent large populations with stratified cohorts (occupation, locality, skills, expectations, social circumstances) and event-driven macro updates. Activate individual simulation around the player and for meaningful personal events, leaders, mentors and unique witnesses.

Compact persistent personality profiles are generated once, then evolve slowly, if at all: personal standards, adaptability, ambition, risk tolerance, conformity tendency, interpersonal disposition, loyalties and other later characteristics. Do not let shared sentiment erase stubborn individual expectations or local variation.

A useful *conceptual* behavioral model (not an implemented universal utility formula):

`behavior(NPC, t) = f(persistent_personality, macro_sentiment, local_circumstances, personal_history)`

The context includes household, worksite, safety, environment, employment, needs, neighborhood, relationships, group memberships and observed opportunities. Use exact constraints for supplies, transport, staff skills and casualties; statistical averages must not disguise critical skill gaps or replace actual people.

## Factions as general social groups

`Faction` can mean family/household network, union, professional association, work crew, guild, business, friendship clique, neighborhood, political group, community or security organization. NPCs may be members of multiple groups simultaneously. A faction forms from real relationships/interests and can persist, split, merge, relocate or dissolve as social history evolves. Faction interpretation of events is **not** global truth.

## Collective capability ≠ expert leader

Workforce proficiency has independent personal, workgroup and organizational dimensions. An NPC with management and leadership at 100 cannot overnight transform an untrained colony. Organization requires repeated work, actual training, proven practices, stable staffing and adjustment time. A formal organization can still be poorly managed. The player remains the ultimate governing authority over player-founded colonies while NPC administrators operate under delegated policies.

## Population accounting

Population, life-support needs, cargo, injuries, deaths, migration and journeys are conserved across aggregation boundaries. Visible passing travelers are materializations of existing persistent journeys/people, not random new spawns. Destroying a transport with 12 people permanently affects the relevant colonies and households (subject to actual survivors). Distant abstract simulations must update authoritative state before detailed encounters occur.

## Migration and governance (future)

NPC relocation responds to comparative economic advantage, employment, commute burdens, living conditions, individual standards and cost of moving. Population moves in both directions; no instant whole-colony relocation when the founder is away. Migration policies may cover open, regulated or closed admission, permits, talents, capacity and enforcement levels of none/lax/medium/strict. Declared enforcement differs from real staffing, training, resources and results, feeding back into colony fitness. Births and deaths are actual transitions, never arbitrary population injection.

## Future APS integration gates

1. Assign persistent individual job skills/experience beyond farming and support vetted save migration from earlier snapshots.
2. Cohort identity, distribution, and exact conservation during individual↔aggregate transitions.
3. Event and consequence ledger for material world changes, later consumed by GRSM.
4. Organizational learning, multiple concurrent faction memberships and shared-sentiment propagation.
5. Variable-rate scheduler with bounded CPU/IO budget, persistence/replay, and small-scale micro-versus-macro outcome comparison.

See [Galactic Reputation & Social Memory](Galactic_Reputation_and_Social_Memory_v0.1.md) for the social-knowledge overlay.
