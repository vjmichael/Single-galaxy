# COLONY-CONFLICT-01 — Independent NPC Colony Offense, Defense and Diplomacy

**Design expansion:** November 3, 2026 (America/Phoenix). Complements `COLONY_DEFENSE_01_SIEGE_CIVILIAN_PROTECTION_2026-11-03.md`.

## Core requirement

NPC settlements can be **autonomous political actors and aggressors**, not only passive defenders. They may initiate conflicts with the player **even when the player has not previously attacked or committed a wrongdoing**. Different settlements have their own leadership, interests, physical resources, beliefs, institutional policies and evolving histories. They do not share one generic faction-brain or a global omniscient targeting database. Each offensive operation must originate from a motivated, identifiable colony and use real population, ships, logistics, weapons, energy/ammunition and risk.

A colony might regard an arriving human as a customer, competitor, intruder, easy prey, political threat, benefactor, stranger, former ally, territorial trespasser or potential rescuer. Those are interpretations, not inherent historical facts. Both a player and an NPC colony should be able to initiate or de-escalate conflict.

## Why colonies might attack first

1. **Territorial ambition:** claim a nearby moon, orbit, salvage field, mineral belt or shipping corridor. A colony's border may clash with a player's otherwise lawful presence; expansionist leaders may issue ultimatums or initiate a blockade to acquire control.
2. **Resource desperation:** poor reserves, disrupted supply lines, starvation or oxygen failure can drive pleading, extortion, interdiction, piracy or desperate raids. A real cargo scan or intel report is required before asserting that a target actually carries supplies; the AI must not assume a ship contains free resources.
3. **Predation/opportunism:** an established colony with a strong fleet may prey on weaker traffic, seize construction sites, charge tribute or organize raids without needing the player to provoke it.
4. **Retaliation and historical grievance:** actual witnessed destruction, casualties, theft, denied rescue, broken agreements, embargoes, social abuse or past violence can trigger revenge or enforcement. The GRSM memory/rumor chain can also distort what an NPC **believes** happened, without mutating the world history.
5. **Security paranoia and pre-emption:** a threatened settlement may confront visitors that approach critical facilities, enforce an announced exclusion zone, warn repeatedly, or prepare a first strike based on **uncertain intelligence**. Unknown ship does not equal enemy; mistaken targeting should be an event with lasting consequences.
6. **Ideology, factionalism, leadership and succession:** a leader may be isolationist, fanatic, imperial, cooperative or corrupt. Individual leaders may disagree, stage coups, replace ineffective defense officers, or redirect scarce capacity into conquest. NPCs can form organizations and other colonies can regard them as aggressors.
7. **Diplomatic cascade:** honoring alliances, collective protection, previous favors and safe-passage agreements may bring NPC colonies into wars the player did not start. Colonies can sue for peace and change policies after outcomes.
8. **Miscalculation:** an attack fails because ammunition runs out, the enemy is stronger than expected, supply lines fail or the target is neutral. Defeat changes available people/resources and future trust, not just a reputation score.

These motivations can combine and conflict. A hungry expansionist can want both grain and territory. A merciful leader may refuse a profitable raid. No player action needs to be illegal for the NPCs to dislike it.

## Escalation ladder

`Observe → Warn → Negotiate/Request Aid → Demand Tribute/Supplies → Trade Restrictions/Blockade → Mobilize/Prepare Raid → Interdict/Pursue → Physically Engage → Retreat/Surrender/Truce/Occupation`.

An NPC may skip some stages only when policy, direct threat, and physical capability support it. A contact record alone cannot authorize firing. A defensive strike, surprise raid, piracy and imperial annexation have different political legitimacy and consequences, but all require the same physically verifiable combat system.

The game should preserve **diplomatic alternatives**, not force every dispute into a shootout. Trade access, tribute, reparations, cooperation, salvage settlement, migration, loans, alliances, free-passage agreements and credible threats can all alter outcomes. A player may influence or replace leadership, but doing so has persistent trust, morale and law/authority consequences.

## Concrete physical conflict

- **Defense:** perimeter sensors, NPC watch, shield/armor, hardened habitats, turret mounts, security shifts, repair staff, shelters and verified evacuation.
- **Offense:** real player/NPC-owned warships, boarding craft, escorts, munitions, supply/fuel depots, weapons loadouts, cargo bays and actual voyage orders. Fleets must travel through the continuous galaxy; range, line of sight, travel time, fatigue and consumed material matter.
- **Targets:** hulls, defensible structures, cargo ships, power grids, life-support facilities, logistics nodes, weapons, and NPC residents with physical location/custody. Attacking civilians must have in-world consequences; children are not special combat loot or objective tokens.
- **Consequences:** injured/deceased named NPCs, ruined outposts, disabled infrastructure, escaped dependents, captured physical stores, salvage/wreckage, migration, depleted ammunition and water, tribunal/diplomacy consequences and changing collective memories. No free replacement crew or magically replenished cargo.
- **Cold simulation:** distant colony hostility can aggregate planned mobilization and logistics, but **physical ships and material handoffs must be promoted and reconciled** when entering warm/detailed combat. Cold abstract “war strength” cannot create ships, workers or casualties out of thin air.

## Implementation phases

**CONFLICT-01A — Evidence-grounded aggression policy** (v82): immutable original local-contact witness + physically present leader + durable colony leadership agenda; deterministic read-only motive/posture; durable independent replay. This is the first code gate; **no actual hostile ship action yet**. An expansionist/predatory leader can initiate tension without prior player wrongdoing, while a cooperative colony observes or asks for aid when hungry. This is a *founding* agenda per colony; future governance must allow revisions through an authenticated policy process, rather than rewriting immutable history.

**CONFLICT-01B — Leadership, grievance and dynamic diplomatic state:** combine actual first-hand GRSM incidents, transmitted/unreliable claims, tenure, morale, internal votes, territory, resources, diplomacy and bounded deterministic personalities; record attitude revisions with provenance and uncertainty. Test false rumors, apology, reparations, victory and defeat.

**CONFLICT-01C — Mobilization and offensive logistics:** actual staffed NPC warship roster, owned ammunition/equipment, power and fuel, manufactured modules and arms, source-to-target route, dispatch queue, return/resupply/repair. No offensive intent upgrades to an attack without real physical means.

**CONFLICT-01D — Authenticated combat, raids, blockades and salvage:** integrate DEF-01B/01C constructed defenses and physical effects, line-of-sight/cooldowns, warnings/engagement rules, actual damage and missing cargo. A successful physical raid can transfer only what the attacking units actually seized.

**CONFLICT-01E — Strategic choices, counterattack, surrender and diplomacy:** multiple colonies autonomously react to war and peace, invite allies, launch counteroffensives, retreat, evacuate civilians, request ransom/reparations, occupy/withdraw and remember attacks. The player can negotiate and be judged differently by different settlements.

**CONFLICT-01F — Real-world and simulation acceptance:** test player-initiated attack, NPC first strike, neutral flyby, misunderstood warning, limited ammo, decisive defense, civil evacuation, post-war repair, false blame and 1–5 year distant colony history with warm/cold reconciliations.

## Balancing player freedom and a living galaxy

The colony-start area may have stronger enforcement and defensive infrastructure; frontier regions allow greater risks and independent power politics. Neither side should be guaranteed invulnerability or an unearned victory. Aggressive colonies should have **recognizable motives, discoverable warnings, limited physical ability, and meaningful responses**. Players can fail, flee, fight, trade, negotiate, fortify, capture or retaliate, but should not be forced into a scripted “everyone hates you” universal war.

**Non-goals:** random aggression without history/agenda, globally coordinated hive-mind NPCs, fake cargo theft, hallucinated attacks, invisible spawn fleets, automatic deletion of populated colonies, RMT/PLEX incentives, and aggression/disposition stored as if it were a proven criminal act.
