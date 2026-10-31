# Galactic Reputation & Social Memory (GRSM)

**Single Galaxy — design specification v0.1**  
**Recorded:** 2026-10-31  
**Status:** Approved gameplay direction; **not implemented** in the B7.0n simulation. Model and thresholds remain provisional unless called out as a fixed design rule.  
**Companion systems:** Adaptive Population Simulation (APS), persistent NPC identities, emerging factions, commerce, persistent ship travel, governance, NPC traits, and ambient dialogue.

## 1. Intent

Give the universe a durable, imperfect, socially distributed memory. A player's heroic deeds, crimes, rescues, piracy, humanitarian actions, privateering, discoveries, and betrayals can all become known. Different NPCs and groups can admire and condemn the same player at the same time, sometimes on the basis of factual information and sometimes on distorted or false stories.

**Central invariant: the game knows what happened; an NPC knows only what they saw, learned, inferred or believed.** Public fame does not imply reliable recognition of the person standing nearby. This system records deeds and beliefs; it does not award a universal moral alignment or imply that the entire world is continuously talking about the player.

### Fixed design principles

1. **Single unified system:** Fame, heroism, infamy, notoriety, achievements, rumors, factual news and folklore use shared identity and historical-event foundations, not independent contradictory game mechanics.
2. **Two logical information stores:** (a) authoritative historical event/evidence records, and (b) distributed NPC/faction knowledge, beliefs and interpretations. They need not be two physical databases.
3. **Competing interpretations coexist:** Any person or organization can store favorable and unfavorable views, contradictory claims, different confidence levels and ambivalence. Do not collapse into one positive-minus-negative score.
4. **Knowledge is not universal:** Information has origin, provenance, propagation delay, access constraints and uncertainty. Highly connected networks can distribute it rapidly; isolated communities can remain uninformed.
5. **Persistent consequences:** A destroyed ship and its real crew remain lost; historical records do not resurrect them, and rumors do not rewrite physical simulation state.
6. **Faction is a broad social-group concept:** Individuals can simultaneously belong to families, unions, guilds, workplaces, neighborhoods, religious groups, political organizations, crews and informal cliques, with conflicting loyalties.
7. **Ambient folklore is rare:** Most NPC interactions concern their own lives. Elaborate player-related storytelling should be a special, occasional event rather than continual protagonist commentary.
8. **Adaptivity and economy:** Use APS macro/group knowledge and event-driven processing. Individual detail is invoked when nearby, personally involved or otherwise consequential.
9. **No universal forgiveness or condemnation:** Helping one community does not erase harm to another. Different groups may reassess judgments gradually and differently as evidence accumulates.
10. **The opening world is empty of established civilizations:** One player colony ship with finite NPC crew; no prebuilt settlements, traffic, police, piracy factions, or ready-made news networks. Such systems emerge over time.

## 2. Two logical information stores

### A. Historical truth and evidence (authoritative)

The simulation records what actually occurred in an append-only, auditable event stream. An event may include:

- `event_id`, game timestamp, coordinate/route, committed simulation transaction reference;
- actor identities and roles; victim/survivor identities, NPC/cargo/ship state changes;
- action category (assistance, raid, theft, murder, defense, rescue, lawful privateering, trade, destruction, discovery, diplomatic agreement, breach, etc.);
- contextual facts (declared jurisdiction, self-defense or contract claims, ownership, authorization, contested legal status);
- effects: who died, who survived, what property was taken, where cargo went, which work positions became vacant, institutional and resource consequences;
- evidence pointers: witnesses, recordings, sensors, recovered wreckage, ledgers, forged evidence records, and possible chain-of-custody uncertainty.

These are canonical *facts of simulation*, not authoritative moral judgments. Legal and ethical interpretations may vary by jurisdiction and culture. A later correction appends a superseding/correction record; it does not silently erase earlier evidence or beliefs.

### B. Distributed knowledge, belief and reputation

The second logical store holds **claims** and **views** by NPCs or faction/community cohorts:

- `claim_id` and optionally related `event_id` (false stories can lack a true event);
- claimant, source, transmission chain, hearsay depth, creation time, latest confirmation time;
- propositions as typed structured claims (numbers, actors, motive, outcome, location);
- evidence known to this observer, assessed source credibility, confidence, contradictions and whether the claim is disputed;
- sentiment about a *subject*, decomposed into admiration, trust, fear/perceived danger, grievance, familiarity, willingness to cooperate and legally recognized standing;
- what this NPC/faction believes about the player **and why**, not just a single approval score;
- direct personal memories can coexist with broader group beliefs; direct eyewitness memory may get priority but is not infallible.

The *two competing databases* metaphor is thus supported without putting positive and negative deeds into separate fact tables. Both truth and rumor may describe admirable and harmful actions; multiple conflicting claims coexist.

## 3. Seven levels of notoriety (recognition scope, not morality)

| Level | Reach / awareness | Typical evidence and dissemination |
|---|---|---|
| 1 — Personal | Directly affected people | Personal knowledge, survivors, immediate witnesses |
| 2 — Local | Nearby people in the immediate locale | Rumors, a few witnesses or substantial evidence items |
| 3 — Colony-wide | Broadly known within a colony | Multiple reports, corroboration, widely accessible local records |
| 4 — Intercolonial | Multiple colonies | Shared dossiers, intercolony routes, communications and institutions |
| 5 — Regional | Broadly known throughout a connected region | Extensive corroboration and fast regional distribution when connected |
| 6 — Interregional | Multiple regions | Interregional intelligence and established news networks |
| 7 — Galactic legend | Widely known across connected galactic society | Galaxy-spanning cultural recognition, historical accounts and fast network distribution |

**Notoriety means *reach of knowledge* about identity/actions, not how dangerous, criminal or admired the player is.** A rescuer and a murderer can both be Level 7. These are descriptive milestones driven by diffusion, evidence, event significance and network reach, not arbitrary points. Stage 5 or 7 "immediate" awareness applies only when information networks actually support fast dissemination; off-grid settlements or pre-network eras remain physically constrained. A galaxy-wide legend need not be visually identified by each person.

## 4. Achievements and incident tags

Attach mechanically earned **factual tags** to the event ledger (for example, `transport_destroyed`, `crew_rescued`, `cargo_seized`, `contract_honored`, `colony_founded`, `civilian_deaths`). The engine may derive provable aggregates from these tags.

Keep **evaluative labels** (`hero`, `pirate`, `liberator`, `traitor`, `criminal`) in the observer's or institution's interpretation, and tie any legal label to jurisdiction and procedure. An authorized privateer to one government may be regarded as a pirate by another. Cargo disposition and treatment of crew and passengers matter. A player can be famous for relief operations and reviled by surviving relatives of past victims at the same time.

Do not force heroic and harmful history to mathematically cancel. Provide dimension-specific views and provenance. Personal grievances may persist despite generalized political pardon.

## 5. Factions and multigroup affiliation

The existing APS faction model supports overlapping memberships. A merchant may simultaneously be an engineering-union member, household member, neighborhood organizer and trade-guild participant. Each group carries beliefs, shared narratives and trust networks. A single NPC combines group influences according to affiliation salience, direct experience, personal principles, incentives, and trust in the particular source.

A faction can promote, contest, distort or ignore a story; it may legitimately disagree with other factions. Both micro (family) and macro (region) group sentiment affect NPC actions. Group beliefs should not override exact individual knowledge or local necessities without causal justification.

## 6. Rumor and folklore engine

**News versus folklore**:
- News is relevant, recent and closer to primary reporting. It may be common immediately after an incident but must still be source- and communication-gated.
- Folklore is a **rare** embellished or socially repeated retelling that can survive long after an incident. It is entertainment and culture, not an authoritative alert.

**Provisional tuning concept:** Above notoriety Level 4, an informal rumor pool may target roughly **40% substantially accurate / 60% embellished, fabricated, omitted or otherwise distorted retellings**. This refers to selected informal retellings, *not* 60% of all facts, news, reports, testimony, beliefs or inhabitants. It is an adjustable narrative heuristic and must not rewrite actual death counts or introduce tangible resources. The mix can vary with institutions, rumor provenance and culture.

Distortion types: inflation of numbers, mistaken actors, omitted context, invented motives, invented exploits, biased heroic framing, biased criminal framing, source laundering, mistaken identity, mixed stories, deliberate propaganda and obviously absurd tall tales.

**Activation gates** (all but explicitly documented exceptions):
1. An actual consequential event or retained story/topic exists.
2. At least one relevant conversant has received the story through plausible channels.
3. Suitable context: NPC conversation rather than active emergency or dangerous task.
4. Global/per-incident and local speaker/location cooldowns and repetition limits allow it.
5. A low narrative selection probability wins against mundane conversation topics.

**Social-setting exception:** In gatherings (cantinas, social events, meals, recreation), an existing conversation can naturally drift toward an older story even if no recent incident occurred. This does **not** override knowledge gating or rarity and does not make NPCs omniscient.

Most ambient speech should address real NPC jobs, personal plans, families, workplace troubles and current social events. Dialogue or optional speech AI merely *renders a selected structured belief* in character; it may embellish within that story's allowed assertions but cannot commit new canonical events or generate additional persons/cargo. Cache common lines, activate only near an observing player, and allow low-chatter options. No continual LLM call per NPC.

## 7. Recognition is a separate probabilistic decision

An NPC who knows the legend does not necessarily recognize the person's face or ship. Inputs can include familiarity with identifying features, source quality, prior encounters, appearance changes/disguise, locale, lighting, social context, belief that the subject could be present, and certainty calibration.

Allow *no recognition*, *correct recognition*, *suspected identification*, or *false identification*. A "Level 7 legend" can walk through a remote settlement unrecognized; another person may be mistaken for them. Once recognized, reactions depend on the NPC's actual opinion and constraints—admiration, fear, suspicion, indifference, trade incentives, duty to enforce jurisdictional law. No automatic hostile-radius rule.

## 8. Computational design under APS

- Event occurrence writes one canonical fact and addresses a bounded set of immediate observers.
- Propagation jobs update colony, faction or region knowledge cohorts at coarse/event-driven cadence; don't individually visit every resident.
- Different subgroup belief distributions preserve minority positions, family-specific memories and professional knowledge.
- Materialize individual memory for firsthand witnesses, victims' relatives, major figures, direct player interactions, and other exceptions.
- Update on significant evidence, social-contact opportunities or relevant decisions, not each world tick.
- Cache location-scoped potential conversations and invalidate on story/knowledge/cooldown change.
- Preserve exact population, casualty, cargo, journey and resource transactions outside rumor simulation.
- Cohort-to-individual promotion must not conjure new historical witnesses or grant the promoted NPC knowledge that their cohort never received.

## 9. Illustrative behavior

**Canonical event:** Player intercepts a freighter, seizes two ore containers; all eight occupants survive. A ship log and one witness record this.  
**Version A:** Merchant guild calls it commercial piracy and fears future raids.  
**Version B:** A frontier relief group claims the ore was distributed to the poor, even though the ledger shows no such distribution.  
**Version C:** A social gathering repeats a tall tale that the player captured twelve freighters without firing a shot.  
**Recognition:** An NPC telling version C may stand beside the actual player without recognizing them.

Positive stories receive exactly the same treatment: a true rescue can become an inflated heroic legend, or a rival faction can spread an unsupported allegation that it was staged. Player notoriety describes how far all this has spread, not whose version is right.

## 10. Safety and fairness to players

- Give the player understandable feedback about reputation effects and the known source of significant reactions, where it makes sense for their character to know.
- Prevent rapid unbounded negative feedback loops from a single trivial act; major consequences should be causally legible.
- Restrict deliberately fabricated stories to observer beliefs rather than canonical convictions/deaths. Institutional actions may still be wrong but should have processes and avenues to contest evidence.
- Historical damage can remain while recovery, forgiveness and new evidence change some judgments. There is no global mandatory forgiveness timer.
- Socially powerful factions do not become automatically truthful. Similar rules apply to NPCs and the player.

## 11. Implementation status and gates

**Implementation update (2026-10-31):** GRSM-01 durable *executed command outcome* history has now been implemented and verified in SQLite schema v5 (see `docs/GRSM01_Durable_Historical_Events_Report_2026-10-31.md`). Rumors, evidence provenance, NPC reputational belief stores, social speech, recognition, notoriety levels, and complete typed physical casualty/ship records are **not implemented**. B7.0n provides opt-in individual IDs, farm XP/mentoring, and standalone stratified cohort arithmetic.

Proposed sequence:
1. **GRSM-01 — Durable events:** canonical committed event IDs, references, transaction/ship/NPC integration, replay and corruption checks.
2. **GRSM-02 — Belief claims:** per-faction and personal claims, provenance, uncertainty, contradiction, correction and knowledge isolation tests.
3. **GRSM-03 — Diffusion and reach:** network-constrained transmission, seven descriptive levels, coexisting faction interpretations, deterministic test scenarios.
4. **GRSM-04 — Identification and responses:** context-sensitive recognition, identity mistakes, lawful/illegal distinctions, reaction triggers that require evidence.
5. **GRSM-05 — Ambient folklore:** rare cooldown-driven event triggers and optional gathering exception; speech renderer with no write access to history.
6. **GRSM-06 — APS scaling:** representative cohorts, individual promotion, long-run persistence, latency and fidelity benchmarks.

### Acceptance examples

- Two factions reach opposite conclusions from one canonical event; the player receives different responses without changing factual history.
- One observer learns about an incident; an isolated colony remains unaware until communication arrives.
- An NPC knows the player's name but does not necessarily identify the player's appearance.
- A rumor falsely claims fatalities; no NPCs actually die and the physical casualty count remains unchanged.
- Two distinct narrative variants remain available while common ordinary-life dialogue dominates.
- An authoritative correction reaches one community but not another; both beliefs persist with provenance.
- Restart/replay and cohort transitions neither duplicate knowledge events nor create/decommission people.

**Open design parameters:** exact level gates, evidence weights, rumor 40:60 tuning, identity-recognition calibration, cooldowns, fade rates, speech/TTS backend, and definitions of sensitive personal memories. These require gameplay and performance testing before finalization.
