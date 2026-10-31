# Single Galaxy

**Original offline-capable space exploration, colony, industry, society and emergent-civilization game (early Linux prototype).**

In the released game's intended opening, one player colony ship with finite NPC crew enters an unexplored fictional galaxy. No stations, gates, populated settlements, free factories or markets are preplaced. The player and later NPC communities develop infrastructure and history through physical work, supplies, travel and decisions.

## Current prototype status

The current engineering baseline is **GRSM-04a** (SQLite v14 firsthand physical-event appraisals, October 31, 2026), descended from the **B7.0n** source/testing archive. The archived evidence records a Linux pass of 54/54 CTest native/integration suites and 84/84 Node tests. The opt-in individual workforce model persists up to 24 original NPC crew identities and farm work/mentoring; a separate statistical cohort prototype demonstrates bounded large-number accounting. **Those are not a live trillion-NPC universe.** The game remains a bounded 2D colony/space prototype, not a continuous 3D multiplayer galaxy. Native Windows and full interactive Chromium playthrough remain open gates.

## Design documentation

- [Galactic Reputation & Social Memory (GRSM)](docs/design/Galactic_Reputation_and_Social_Memory_v0.1.md): canonical deeds versus conflicting NPC/faction beliefs; seven notoriety reach levels; heroism, crime, evidence, recognition, rare folklore.
- [Adaptive Population Simulation and Factions](docs/design/Adaptive_Population_Simulation_and_Factions_v0.1.md): macro/micro layering, cohort distributions, personalities, overlapping social groups.
- [Decisions register](docs/design/Core_Decisions_2026-10-31.md): game continuity, governance, migration, society and open design choices.
- [Engineering next gates](docs/engineering/NEXT_GATES_2026-10-31.md): verified current capabilities versus proposed code.
- [B7.0n archived source README](docs/engineering/README_B7_0n_archived.md): earlier milestone-by-milestone project overview and run commands.

## Build and test (Linux)

Requires the toolchain/dependencies described in the archived project documentation (C++/CMake, Ninja where used, Node for viewer tests, SQLite dependency as appropriate to environment).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure
node --test tests/*.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

Tests and development are local-first. This repository is for version control, code provenance, milestone history and documentation; it is not intended to make feature progress depend on CI availability.

## Status warning

The foundational GRSM event, evidence, testimony, social-group, directional trust and membership slices are implemented. **Automatic gossip**, personality-driven faction formation, multi-colony migration, persistent passenger traffic, voice rumors, galaxy-scale APS and full autonomous colony governance remain **proposals**, not implemented systems. See milestone reports for actual feature evidence.

## License / distribution

**License and bundled asset provenance are pending review.** No open-source license is granted by this repository document. Keep a new hosted repository private until the user chooses a distribution policy and source/third-party provenance is audited.

## GRSM-01 · Verified historical-event foundation (2026-10-31)

The first Galactic Reputation & Social Memory engineering gate adds an append-only, SQLite v5 ledger of **executed command outcomes**, written atomically with the physical world state and validated against snapshots, receipts and journal on restart. It is **not** a rumor or social-reputation engine. See [`docs/GRSM01_Durable_Historical_Events_Report_2026-10-31.md`](docs/GRSM01_Durable_Historical_Events_Report_2026-10-31.md) for boundaries, migration and test evidence. Local Linux regression: 55/55 CTest and 84/84 Node tests.

## GRSM-01b · Physical incident ledger (2026-10-31)

SQLite schema v6 and kernel snapshot v21 now record exact physical movements, completed journeys, persistent wrecks, and named ship-crew casualties. Ship destruction and casualties are **host-only test transitions**, not implemented player combat; no commercial passenger transport AI or reputation decisions are claimed. Read [`docs/GRSM01b_Physical_Incidents_Report_2026-10-31.md`](docs/GRSM01b_Physical_Incidents_Report_2026-10-31.md). Local Linux checks: 56/56 CTest native/integration and 84/84 Node tests.

## GRSM-02 · NPC evidence and conflicting testimony (2026-10-31)

SQLite schema v7 adds host-attested local observation and provenance-linked NPC-to-NPC testimony **without changing physical truth**. A surviving NPC can receive faithful, embellished, or contrary stories, and contradictory accounts remain separately recorded. Direct knowledge requires a new nearby incident; migration never invents retroactive witnesses. No automatic gossip, faction standings, notoriety levels, LLM-generated chatter, or speech AI is claimed. See [`docs/GRSM02_Evidence_Claims_Provenance_Report_2026-10-31.md`](docs/GRSM02_Evidence_Claims_Provenance_Report_2026-10-31.md). Local Linux regression: 57/57 CTest, 84/84 Node.js.

## GRSM-03a · Local social groups and witnessed relays (2026-10-31)

SQLite schema v8 adds overlapping groups founded from existing, co-located workforce identities; only real member-to-member local contact can add an individual group-context claim. Each group can retain multiple *interpretations* of one incident, with provenance to the claim that prompted it; absent members receive no automatic knowledge. This is a small host-controlled gate, **not** automatic gossip, a faction consensus mechanism, universal recognition, or an intercolonial media network. See [`docs/GRSM03_Social_Groups_Relays_Report_2026-10-31.md`](docs/GRSM03_Social_Groups_Relays_Report_2026-10-31.md) for testing and integrity limitations.

## GRSM-03b · Directed social trust (2026-10-31)

GRSM-03b adds an optional host-controlled **trust-gated individual testimony path** (SQLite v9): physically grounded directed contacts, claim-specific trust reassessment, and confidence capped by trust and the actual source chain. Old host APIs are unchanged; this is **not** autonomous faction formation, mandatory trust policy, or omniscient NPC gossip. See [GRSM-03b engineering evidence](docs/GRSM03b_Directional_Trust_Evidence_Report_2026-10-31.md) for scope and verified tests.

## GRSM-03c · Changing memberships and receipt-time group eligibility (2026-10-31)

SQLite v10 now supports **durable, event-sourced group joins, exits and rejoins** by living workforce NPCs. Joining requires an existing, physically co-located member as sponsor; group claims capture immutable membership sequence receipts so later departures do not rewrite past social history. A group does not broadcast facts by membership alone, and no automatic gossip, comm-network, social decay, player recognition, or autonomous politics is claimed. See [`docs/GRSM03c_Membership_Epochs_Report_2026-10-31.md`](docs/GRSM03c_Membership_Epochs_Report_2026-10-31.md).


## GRSM-03c.2 · Durable bounded social-contact opportunities (2026-10-31; Linux tested)

SQLite schema v11 now supports **explicit host-invoked, bounded NPC encounter scheduling** for surviving NPCs at the same real location, with a durable scheduling cursor, append-only encounter and scan receipts, per-pair familiarity counts, repeat-contact cooldown, migration boundaries, and recovery/tamper validation. Encounter eligibility is not evidence of talking, trusting, group affiliation, information transfer or intercolony communication. See [GRSM-03c.2 report](docs/GRSM03c2_Bounded_Encounter_Scheduler_Report_2026-10-31.md) and [acceptance contract](docs/engineering/GRSM03c2_Implementation_and_Acceptance_Plan_2026-10-31.md).

**Current acceptance (post-hardening):** Linux full native/integration CTest **61/61**, Node **84/84**, Python design **3/3**, repeated four-relay smoke **12/12**. Earlier legacy timing failures are preserved in the logs; this establishes a final-code Linux pass, not proof that all timing races are impossible. No Windows-native end-to-end or full interactive Chromium verification is claimed.

**Verification qualification for GRSM-03c.1:** targeted native suites 5/5, Node 84/84, Python design 3/3, full Linux compilation successful. A pre-final-code full CTest pass was 60/60; after final guards, complete runs at 59/60, 59/60 and 58/60 exposed intermittent older live-relay failures. Failed cases later passed individually, and that historical 03c.1 final-code gate remained open at the time. Subsequent **GRSM-03c.2** hardening obtained a complete 61/61 native pass. See [GRSM-03c test evidence](docs/GRSM03c_Membership_Epochs_Report_2026-10-31.md).


## GRSM-03c.3 · Directed rapport from witnessed opportunities (2026-10-31; Linux tested)

A new, **explicit host-attested** social-interaction API links each Assistance/Harm reaction to an actual same-tick GRSM-03c.2 encounter. The result is a durable **directional rapport** score, independent of GRSM-03b testimony trust, legal standing, group sentiment and world history. Encounter alone awards no rapport. SQLite v12 migration invents no historical interactions; maliciously edited ordinary social rows fail restore validation. This prototype does **not** independently infer whether an action was helpful/harmful from executed physical effects and does not generate autonomous NPC decisions. See [GRSM-03c.3 report](docs/GRSM03c3_Directional_Rapport_Report_2026-10-31.md).

**Linux acceptance:** 62/62 CTest, 84/84 Node.js, 3/3 Python design, and 12/12 stressed legacy relay checks across three repetitions. Windows-native play and full independent physical-event attribution remain open gates.


## GRSM-03d · Conflicting, overlapping individual views of other groups (2026-10-31)

GRSM-03d adds durable, host-attested **individual evaluations of one group by members of another**, linked to actual same-tick social interaction records and immutable historical membership epochs. Different members can **agree, disagree, or remain uninformed**, one NPC can assess another group differently through two overlapping affiliations, and the reverse group's opinions need not match. The group relation summary is a **descriptive distribution among informed active members**, not a group vote, institutional charter, sovereign policy or collective mind. SQLite v13 migrations from older worlds invent no historic group positions.

**Verified locally on Linux:** native/integration CTest **63/63**, Node **84/84**, Python design **3/3**. An initial 62/63 run exposed an outdated v1 migration fixture; the fixture was corrected and a new complete 63/63 run passed. Earlier failure logs remain archived. See [GRSM-03d engineering report](docs/GRSM03d_Intergroup_Perspectives_Report_2026-10-31.md) and the [nested loyalty / macro-group design](docs/engineering/GRSM03d_Group_Opinions_And_Nested_Loyalties_2026-10-31.md).

**Not yet implemented:** automatic classification of real physical help/harm, NPC political choices, multiaxis loyalty, nested organization authority, voting, secession, federation-wide consensus, or remote knowledge delivery. The social prototype is host-invoked and small-roster. Windows-native/UI acceptance remains open.


## GRSM-04a · Physical evidence–linked individual appraisals (2026-10-31)

A local NPC who has an **actual firsthand witness receipt of a committed physical incident** may express a subjective appraisal in an unaffiliated or verified group-membership context. The same ore movement can produce an appreciative miner-guild opinion and a damaging relief-group interpretation without changing the objective event. A hearsay claim or remote NPC cannot masquerade as direct physical proof. SQLite v14 creates append-only appraisal history with original membership epochs, migration without retrospective opinions, integrity/recovery checks and replay limits.

**Linux acceptance:** native/integration CTest **64/64**, Node **84/84**, Python design **3/3**, targeted social/persistence suites **11/11**; earlier interrupted unbounded full run is recorded, not counted. [GRSM-04a engineering report](docs/GRSM04a_Physical_Evidence_Appraisals_Report_2026-10-31.md).

**Still future work:** independently proving which particular NPC caused an effect, automatic interpretation of physical consequences, autonomous decisions, federation government, long-range information routes and Windows-native/visual manual acceptance. Physical proof of ore moved is not proof an individual stole it.
