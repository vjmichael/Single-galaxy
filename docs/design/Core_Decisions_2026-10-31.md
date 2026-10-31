# Single Galaxy — Design Decisions Register

**Recorded 2026-10-31.** These decisions preserve the direction established in collaborative design discussions; they are **not** claims that the mechanics already ship.

## Opening state and player's authority

- A single fictional unexplored galaxy; the player starts with **one colony ship, a finite NPC crew** (24 is a test fixture, not finalized population balance), no built cities, stations, stargates, roads, routes, markets, rival settlements or pre-generated NPC civilizations.
- All infrastructure requires actual labor, materials and operations. Inventories and people occupy physical locations. No spontaneous free population, cargo, or resurrection.
- Player retains ultimate authority over colonies they founded, even following years away. Delegated NPC managers cannot revoke this; they may have realistic social disagreements. Leadership of a genuinely independent NPC-founded community must be earned later through its own institutions.
- Managed absence must not become an inactivity tax; delegated operations consume real resources, use standing instructions, and provide a meaningful return briefing.

## Society and labor

- Informal labor, voluntary contribution, specialization, appointed managers, schedules, formal employment and multi-department organizations emerge **gradually** through distributed proficiency. Formal structure does not guarantee competent administration.
- Training adds job XP; a mentor can mentor **one** apprentice at a time, with proficiency-gap-limited bonuses.
- Personal living standards and expectations can be stubborn; happiness, trust and grievance are not easily manufactured by bonuses. Same settlement may delight newcomers and disappoint longtime residents.
- Social stability, conformity and cohesion are separate indicators. Shared norms may ease coordination but are not automatic proof of stability; suppressing grievance can conceal fragility.
- Personality and accumulated relations eventually affect management, faction behavior and reactions. Real social pressure can moderate outliers without making all dissent illegitimate.

## Population dynamics and settlement history

- Settlement development occurs through meaningful opportunities: distant valuable work, expensive commutes, labor demand, housing, services, and sustainable logistics. An entire existing colony may *consider* shifting its economic center only after sustained net advantage and credible relocation costs. A founder-authorized policy governs major relocation.
- Bidirectional migration: poor management can lose residents; attractive settlements can gain workers from struggling NPC-run colonies. Population and skills follow real persons and transports. NPCs may voluntarily relocate, but adventure-seeking alone is not a founding-colony trigger.
- Social groups ("factions") are overlapping, locally founded affiliations, not just hostile political/military blocs.
- A six-real-year colony is precious: loss remains consequential, but should be caused by world events, not hidden inactivity punishments.

## Notoriety, recognition and folklore

- Unify heroic and notorious history into GRSM, consisting of canonical deeds and distributed conflicting claims. Heroism and villainy are interpreted by people and groups; one action can produce opposite views.
- Seven notoriety levels describe reach of recognition/information, not criminal severity. Face recognition remains conditional, not automatic.
- Above Level 4, a **provisional** 40% substantially accurate / 60% exaggerated-fabricated **informal retellings** mix is a configurable story-generation target, not a statement about official reports or all NPC beliefs.
- Most NPC dialogue is ordinary life. Folklore appearances are rare event/cooldown-controlled Easter eggs, with context-appropriate social-gathering resurfacing of old tales.
- Actual wrecked transports, deceased crews, cargo, family effects, economy and justice are authoritative; rumors can distort descriptions but do not alter physics or casualties.

## Status and deferral

B7.0n implemented on Linux: opt-in 24 persistent individual crew records, limited farm XP/mentoring/team experience, SQLite v20, and a **separate** stratified population arithmetic prototype. It has not implemented migration, factions, personality simulation, GRSM, persistent passenger-ship traffic, full autonomous growth or the galaxy-wide APS scheduler. Test status belongs in milestone reports; the design register must not be read as a completion report.
