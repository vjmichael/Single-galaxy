# Single Galaxy

**Original offline-capable space exploration, colony, industry, society and emergent-civilization game (early Linux prototype).**

In the released game's intended opening, one player colony ship with finite NPC crew enters an unexplored fictional galaxy. No stations, gates, populated settlements, free factories or markets are preplaced. The player and later NPC communities develop infrastructure and history through physical work, supplies, travel and decisions.

## Current prototype status

The latest committed engineering integration is **GRSM-04b.B2a** (SQLite v17 named shipboard fabrication, October 31, 2026); **ECON-02B0** adds a separately tested quantified production domain kernel that is **not yet wired into PersistentWorld**. The original historical baseline descends from the **B7.0n** source/testing archive. The archived evidence records a Linux pass of 54/54 CTest native/integration suites and 84/84 Node tests. The opt-in individual workforce model persists up to 24 original NPC crew identities and farm work/mentoring; a separate statistical cohort prototype demonstrates bounded large-number accounting. **Those are not a live trillion-NPC universe.** The game remains a bounded 2D colony/space prototype, not a continuous 3D multiplayer galaxy. Native Windows and full interactive Chromium playthrough remain open gates.

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


## GRSM-04b.A · Verified named NPC roles without fabricated culprit (2026-10-31)

SQLite v15 adds append-only receipts recording only **NPC victims directly identified by committed NpcLost activity** and **firsthand observers possessing their own validated local-observation source**. These roles are cross-checked against physical event and NPC knowledge ledgers on restoration, survive recovery, and cannot be forged by replaying hearsay, substituting crew members, or retroactively declaring an old incident. The v14-to-v15 migration invents no earlier roles or opinions.

**Linux local verification:** complete final native/integration CTest **65/65**, Node.js **84/84**, Python design **3/3**, persistence/social **12/12**. An earlier whole-suite run was interrupted and is retained as a separate, nonpassing trace. See [GRSM-04b.A engineering report](docs/GRSM04bA_Verified_Roles_Report_2026-10-31.md).

**Crucial limitation:** a player/host command naming a **ship** does not establish a particular **NPC actor**, perpetrator, beneficiary or culpable organization. GRSM-04b.B must implement independently committed NPC job/action-effect receipts before individual culpability can be inferred. No automatic blame, punishment, dialogue, social-policy change, autonomous political behavior, Windows-native or Chromium full playthrough is claimed in this slice.


## GRSM-04b.B1 · Named NPC mining work with committed effect proof (2026-10-31)

An explicitly authorized host pathway now accepts a **specific living onboard crew member** as the assigned executor of a real mining command. SQLite v16 records an immutable assignment separately from an **executed-work receipt**. The latter is written only with the successfully applied simulation step and a genuine positive ore-transfer incident. A failed command, ordinary ship-level mining, or a fellow passenger can never be retroactively credited as named work merely by association. The source is a host-owned job appointment plus independent committed physical outcome, **not yet** simulation of an NPC independently selecting the task or operating equipment. Replays, restarts, recovery, tampered identity and physical-source rows, and migrations from v15 are tested.

**Linux verified:** complete native/integration CTest **66/66**, Node.js **84/84**, design tests **3/3**, targeted persistence/GRSM **13/13**. Earlier incomplete native runs and a separately passed legacy relay are retained. [GRSM-04b.B1 engineering report](docs/GRSM04bB1_Named_Mining_Execution_Report_2026-10-31.md).

**Deferred network and launcher decision:** use a future **ID-centric versioned binary multiplayer protocol** over an authenticated transport; retain a single authoritative simulation whether single-player or dedicated-hosted. Eventually a single-click game launcher will start the local server invisibly and an optional separate **Server Manager GUI** can manage the same backend. These remain **design decisions, not present functionality**. See [NET-01 deferred transport / hosting design](docs/engineering/NET01_Deferred_Binary_Transport_And_Dual_Mode_Launcher_2026-10-31.md).

**Not yet implemented:** generalized named NPC work across professions, causally validated individual assistance/harm in arbitrary commands, commander/executor/beneficiary attribution, autonomous NPC choices, legal culpability, a production multiplayer protocol, a single-click native launcher or a server GUI. Windows-native and full interactive UI acceptance remain future gates.


## ECON-02A — Universal workshop and basic/advanced occupations (2026-10-31)

The new **C++17 ECON-02A domain prototype** introduces one configurable workshop with a multi-stage recipe graph (aluminum feedstock → ingots → sheets/wire → brackets/valves → hull panels/pumps), explicit material input reservations, single-task-per-worker scheduling, selectable powered workstations, shortage proposals requiring authorization, and completed-work receipts identifying the worker. Basic work accepts a zero-experience volunteer; advanced assembly requires combined proficiencies. Seven example recipes use intentionally simplified test masses.

**Scope:** This model compiles with `galaxy_kernel` but is **not yet wired into PersistentWorld, actual colony stores, SQLite work-order persistence, or GRSM personal/social effects**. It does not claim automated business formation, jobs/wages, real ship fuel, a minted currency or a complete NPC labor department.

**Verification:** Linux all-target build passed; dedicated test **1/1**, non-live CTest **42/42**, Node **84/84**, design **3/3**. An attempted complete CTest **did not finish** in legacy live-relay scenarios and remains open; do not count that as 67/67. See [ECON-02A engineering report](docs/engineering/ECON02A_Universal_Workshop_Labor_Board_and_Skill_Convergence_2026-10-31.md) and [global money-slang/name research](docs/engineering/ECON02A_Global_Money_Slang_Currency_Name_Research_2026-10-31.md).

**Development order:** retain GRSM-04b.B1 / SQLite v16 as the *latest verified durable game integration*, with ECON-02A as the latest *standalone economy domain*. Next join the two by binding the workshop's actual inputs/outputs and named labor completion to transaction-safe simulation receipts. Defer universal multiplayer transport and launcher UI as specified in NET-01.


## 2026-10-31 · GRSM-04b.B2a + ECON-02B0 engineering convergence

**B2a:** Real shipboard module fabrication can be assigned to a named, living onboard NPC, with assignment distinguished from successfully committed execution proof. Failed commands do not award execution credit. See [named fabrication report](docs/engineering/GRSM04bB2a_Named_Fabrication_Report_2026-10-31.md).

**ECON-02B0:** Added the standalone deterministic quantified production kernel. Every ingredient must meet the machine-tier minimum before a batch begins; below-minimum material generates **no output, no scrap, and no input consumption**. Whole valid batches reserve input, finite finished/scrap containers enforce backpressure, and in-progress batches can resume from validated state copies. Remote aggregated production matches tick-by-tick production in deterministic no-intervention tests. See [quantified production report](docs/ECON02B0_Quantified_Production_Engineering_Report_2026-10-31.md).

**Local evidence:** ECON-02B0 all-target Linux build, 44/44 non-live native suites, 84/84 Node tests, 3/3 Python design checks. The entire live E2E suite, Windows-native integration, and a persistent outpost manufacturing pipeline remain open. See [factory design and gates](docs/engineering/ECON02B1_Factory_Design_And_Next_Gates_2026-10-31.md).


## ECON-02B1a · Real outpost workshop integration (2026-10-31; Linux local tested)

A **single installed Tier 1 outpost processor** now consumes an actual colony module for installation and uses physical on-site aluminum shipments, a named local NPC operator, real utility power, a five-tick reserved 20 kg batch, finite finished/scrap buffers, and durable SQLite recovery. Valid batches yield 19 kg aluminum panels and 1 kg conserved scrap; below-minimum inputs produce nothing. Completed products ship in conservative whole-tonne composite pallets until cargo precision is upgraded. This is **not yet** freeform machine placement, line networks, autonomous factory planning, or full-world aggregated fast-forward. See [ECON-02B1a engineering report](docs/engineering/ECON02B1a_Outpost_Workshop_Real_Inventory_2026-10-31.md).

Local Linux evidence: 45/45 non-live native CTest suites, 84/84 Node tests, 3/3 Python design tests; full live E2E/Windows/GUI playthrough remains open. GitHub stores source and reports, not the development hub.

## ECON-02B1b — Two-machine outpost production with backpressure (2026-10-31)

The persistent outpost workshop can now install a second, material-paid Tier 1 machine, assign a **different real local NPC operator**, consume the first machine's bounded panel-output stock, and create durable aluminum-frame stock plus physical scrap. Every stage needs a complete minimum batch; unavailable inputs cause no new input consumption/output/scrap. Both stages share finite intermediate and scrap stores. A filled final bin blocks the finisher, which eventually blocks its supplier. Power and maintenance remain real dependencies. Output can be moved back to the ship in a conserved 1t batch under legacy cargo constraints. Snapshot format `galaxy-kernel-v23` survives restarts and recovery; ledger checks reject forged material-transfer destinations.

**Local verification:** full Linux build; non-live native CTest **46/46**, Node **84/84**, design Python **3/3**; targeted real-world and recovery tests pass. **No** world-scale accelerated remote simulation, actual conveyor entity/path throughput, general recipe graph, spatial floor-plan builder, native Windows/UI acceptance or full live E2E is claimed. Read [ECON-02B1b engineering report](docs/engineering/ECON02B1b_Connected_Outpost_Line_Engineering_Report_2026-10-31.md) for exact scope and acceptance.


**Final broader Linux acceptance (same source revision):** all **71/71** native CTest cases passed, including the previously intermittent legacy live/relay scenarios (parallel local run with per-test timeout). This is Linux evidence only; Windows-native and browser GUI acceptance remain open.

## ECON-02B1c — Metric-volume cargo and independent item stacks (2026-10-31)

Ships now enforce **m³ game-volume capacity only; no payload mass limit**. Versioned `TransferWorkshopStack` actions move exact material stack quantities individually (aluminum, panels, frames and scrap) between a player-owned outpost workshop and its nearby colony ship. The previous one-tonne mixed-output pallets remain supported solely for older saved commands, not as a requirement for new transfers. Stock, output-space, authority and conservation are still hard constraints, while current per-item volume coefficients are initial game-balancing values rather than real-world density. `galaxy-kernel-v24` remains backward-readable with earlier snapshots; committed stack transfer endpoints are independently verified on SQLite recovery.

**Local Linux verification:** complete Debug build, full **72/72** native CTest, **84/84** Node, **3/3** Python design tests. This is a simulation and persistence milestone, **not** the cargo GUI or finished spatial factory logistics. See [ECON-02B1c report](docs/engineering/ECON02B1c_Metric_Stack_Cargo_Report_2026-10-31.md).

## ECON-02B2a — Catalogue-backed volume stacks (2026-10-31)

Reused the B1c persistent v24 world while introducing a common, validated game-volume item catalogue and mixed-stack inventory value API. The **real ship↔workshop exact-stack command** now validates against that shared capacity/quantity contract before the authoritative world effects are committed. New read-only ship/bin inventory views prepare cargo UI and later utility equipment; no weight/density simulation was added. **Local Linux verification:** 73/73 native CTest (including legacy live simulations), 84/84 Node, 3/3 Python design. Physical installable containers, powered conveyors, freeform factory editor and NPC-generated plants remain open. See [ECON-02B2a engineering report](docs/engineering/ECON02B2a_Catalogue_Stack_Inventory_Report_2026-10-31.md).

## ECON-02B2b — Constructed outpost storage (2026-10-31; local Linux verified)

A real player-owned outpost can now construct a **4 m³ mixed-stack container** from one produced module. Authorized nearby ships can move exact quantities among ship cargo, container, and installed workshop bins, with all-or-nothing m³ capacity checks. Each valid transfer and construction receives an SQLite journal/ledger receipt; snapshot `galaxy-kernel-v25` records and validates physical storage while older saves remain readable. **Local verification:** full CTest 74/74, Node 84/84, Python design 3/3. This is an initial site-bound container for four workshop goods, not arbitrary player-placed warehouses or conveyors. See [ECON-02B2b engineering report](docs/engineering/ECON02B2b_Persistent_Outpost_Storage_Report_2026-10-31.md).

## ECON-02B2c1 — Directed multi-container logistics domain (2026-10-31)

`InventoryLogistics.hpp/.cpp` introduce a reusable, deterministic multi-node transport model using `StackStore`'s **game-volume** cargo system. Stable container/route IDs, strict item filters, per-tick rates, deterministic shared-source priority and complete storage backpressure are exercised by a new native test. One-link remote advancement is O(1) and inactive networks fast-forward. **The graph is not yet connected to PersistentWorld**: physical construction, on-map links, authorization, SQLite receipts and NPC plans remain ECON-02B2c2 gates. The existing one-container-per-outpost B2b feature is unchanged; no snapshot/SQLite schema migration in this increment. Local verification: full Linux build; **50/50 non-live CTest**, **84/84 Node**, **3/3 Python**; full live suite incomplete. See [ECON-02B2c1 engineering report](docs/engineering/ECON02B2c1_Directed_Logistics_Domain_2026-10-31.md).

## ECON-02B2c2 — Durable multiple outpost containers (2026-10-31)

Following B2c1's transport-domain foundation, the actual persistent outpost simulation now permits constructing additional independently bounded **4m³ item containers** with real construction modules and stable site-local IDs. Existing node 0 remains compatible with older saves, and new node transfers obey authorized ship/workshop proximity, all-or-nothing quantities, and per-container backpressure. Snapshot **v26** and SQLite journal-backed construction/transfer resource receipts restore exact stock and reject forged endpoint records; no schema migration was necessary. The standalone transport domain is **not yet wired to automatic physical conveyor links**—that is ECON-02B2c3. Linux final-code acceptance: **51/51 non-live CTest, 84/84 Node, 3/3 Python**, all-target compile passed. Native Windows, full live and GUI acceptance remain open. See [ECON-02B2c2 engineering report](docs/engineering/ECON02B2c2_Durable_Multi_Container_Outpost_2026-10-31.md).

## ECON-02B2c3 — Module-backed automatic outpost logistics (2026-10-31)

Real constructed outpost storage containers can now be connected by host-authorized, one-way, item-filtered and rate-limited transport links. Each link consumes an actual construction module, advances in deterministic route-ID order, transfers stock between independently finite 4 m³ containers, and stops for input shortages or full destinations. Snapshot **v27** persists link configuration and cumulative movement; SQLite records individual automatic transfer effects transactionally and rejects forged endpoints during recovery. No cargo weight/payload restrictions are introduced.

**Local Linux verification:** 77/77 full native CTest including legacy live/relay, 53/53 non-live native, 84/84 Node, 3/3 Python design tests. **Not yet implemented:** visual conveyor placement, electrical load and maintenance, arbitrary filter configurators, aggregated remote-world route scheduling, native Windows graphical acceptance. See [ECON-02B2c3 engineering report](docs/engineering/ECON02B2c3_Durable_Automated_Outpost_Links_2026-10-31.md).
