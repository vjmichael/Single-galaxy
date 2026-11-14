# Engineering Handoff — 2026-10-31

> **CURRENT PRIORITY OVERRIDE (2026-11-13):** This historical handoff
> is retained for provenance; work priorities have moved to
> [MASTER_TASK_LEDGER.md](MASTER_TASK_LEDGER.md).
> Repository integration into a single `main` is P0. No new branches/PRs.
> Windows/Linux CI is batched at completed integration-group boundaries
> under [CI_TASK_GROUP_ACCEPTANCE_POLICY.md](CI_TASK_GROUP_ACCEPTANCE_POLICY.md).
>

## Latest verified artifact

`SingleGalaxy_B7_0n_Individual_Workforce_Cohorts_Source_Tests_2026-10-31.zip` is the baseline of this repository. It was extracted to its contents without changing its C++/JS program files for this documentation commit.

The associated milestone report records: Linux CTest **54/54**, Node **84/84**, and a production-style Linux build. These are **historical results from B7.0n**, not rerun in the documentation-only commit. Windows-native, Chromium manual UI, continuous 3D galaxy and full APS remain unverified.

## Immediate engineering work

1. Create a durable canonical NPC/ship/world event journal with transaction references and unique stable IDs, separate from display and rumor generation.
2. Complete worker role coverage beyond farm/habitat, with individual assignments and bounded autonomy, preserving real life-support consumption and resident capacity.
3. Journaled migration path from old aggregate saves to opt-in individual mode with rollback/recovery tests.
4. Extend the cohort module to persistent sector/colony state and require explicit exactness/uncertainty boundaries when promoting an NPC to individual detail.
5. Add GRSM-01/02 event and claim contracts with isolation/corruption/replay tests before generating any dialogue.
6. After these gates, implement faction affiliation and macro sentiment plus variable-rate APS scheduling, then rarity-gated folklore presentation.

## Test commands

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure
node --test tests/*.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

Use bounded regression groups if older slow gate tests stall. Preserve failing/intermittent evidence. GitHub is a repository and documentation record, not the primary development execution environment.

## Repository governance

- Keep original simulation changes isolated from Lu, Cognitum, and historical EVEmu audit code.
- Do not add a license or publish publicly before provenance/license review and user visibility choice.
- New GRSM specifications are prospective: **no implemented GRSM claims** until integration and acceptance tests pass.

## 2026-10-31 update: GRSM milestones

- **GRSM-01 / 01b:** committed executed-command truth, physical incidents, durable wrecks, shipments and named losses.
- **GRSM-02:** committed local observation and NPC-to-NPC provenance-linked testimony; see `docs/GRSM02_Evidence_Claims_Provenance_Report_2026-10-31.md`.
- **GRSM-03a local working gate:** overlapping founded groups, member-to-member contact, durable conflicting discussion views; see `docs/GRSM03_Social_Groups_Relays_Report_2026-10-31.md` for precise test qualifications and known checkpoint-compaction limitation. **Not a complete faction/APS/reputation system.**

**Next engineering priority:** GRSM-03b contact schedule/communication-topology evidence and institutional memory, with bounded worker processing, non-universal diffusion and durability under old checkpoint compaction. Do not add galaxy-wide broadcasting merely from membership or notoriety tier.


## 2026-10-31 update: GRSM-03b verified trust slice

- **GRSM-03b:** completed bounded directional NPC contacts, knowledge-linked
  subjective trust transitions and an opt-in trust-gated relay path with
  transaction-time confidence caps. New SQLite v9 migration fabricates no old
  relationships. Explicit host-only APIs; **not** autonomous NPC gossip.
- **Local Linux Debug verification:** **59/59 CTest**, **84/84 Node** and
  **3/3 Python design tests** passed. See
  `docs/GRSM03b_Directional_Trust_Evidence_Report_2026-10-31.md`.
- **Next gate GRSM-03c:** versioned join/leave group membership, event-based
  social-contact scheduling, and bounded message routes with physical travel
  or established communication infrastructure. Harden old-contact receipts
  after snapshot compaction. No instant intercolony news or mandatory universal
  trust policy in this slice.

## 2026-10-31 update: GRSM-03c membership epochs

- **Implemented and locally tested** SQLite v10 append-only membership epochs: co-located sponsor for voluntary join/rejoin, surviving member exit, durable receipts, member-only relay eligibility evaluated inside the claim transaction, current-membership validation for group views, recovery/migration and tamper scenarios.
- **Local Linux verification:** native full build succeeded; core targeted 5/5 suites, Node 84/84 and design 3/3 passed. One full pre-final-code run passed 60/60; later full reruns had intermittent legacy relay failures (59/60, 59/60, 58/60), each failed case subsequently passing in isolation. The final full-suite gate remains **open**, and exact logs are preserved in the GRSM-03c archive. See `docs/GRSM03c_Membership_Epochs_Report_2026-10-31.md`.
- **Remaining GRSM-03c.2:** bounded autonomous contact opportunity scheduling, communication topology/travel proof, and relationship/contact aging rules. These are *not* present in GRSM-03c's membership gate and must not be reported as working.
- **GRSM-04 afterward:** ratified group policy versus personal interpretation, actual faction governance/leadership and APS-cohort knowledge routing. All future work remains locally tested and source-isolated from Lu/Cognitum/EVEmu archival research.


## 2026-10-31 update: GRSM-03c.2 planning and documentation status

- The current code baseline is **GRSM-03c.1**, SQLite v10 with persistent join/leave/rejoin membership epochs, group-relay eligibility at record time, and preserved pre-v10 social history. This replaces the outdated B7.0n-only current-status language at the beginning of this document; B7.0n remains an archived starting point.
- **Verification: qualified, not fully green.** Native build, targeted 5/5 suites, Node 84/84, design 3/3 passed. A pre-final-code native full pass was 60/60, followed by final-code complete-suite runs at 59/60, 59/60 and 58/60 with intermittent older live relay failures. Individual reruns passed; root cause remains open.
- **03c.2 active priority:** deterministic bounded real-contact opportunities, explicit host scheduling, append-only receipts, no automatic rumors or cross-colony broadcasts, migration/recovery integrity and legacy relay readiness stabilization.
- **Acceptance contract and scoping:** [GRSM-03c.2 implementation plan](GRSM03c2_Implementation_and_Acceptance_Plan_2026-10-31.md). Gate 03c.2-C explicitly requires a *final-code complete native suite* before the whole milestone can be called green.
- Keep original Single Galaxy source isolated from EVEmu archival research, Lu and Cognitum; GitHub is a source/version record, not the principal local development environment.


## 2026-10-31 update: GRSM-03c.2 completed Linux acceptance

- **Current baseline:** GRSM-03c.2, SQLite schema v11, explicit host-invoked bounded co-located encounters, durable round-robin candidate cursor, minimum per-pair cooldown, contact history/familiarity count, ordered scan and contact receipts, integrity/recovery/migration tests. GRSM-03c.1 is the historical membership foundation, **not the current baseline**.
- **Latest verified final code:** Debug Linux full CTest **61/61**, Node **84/84**, Python design **3/3**, and three consecutive parallel relay smoke rounds **12/12**. Source and both passing/failing historical execution logs: [GRSM-03c.2 engineering report](../GRSM03c2_Bounded_Encounter_Scheduler_Report_2026-10-31.md). This is a positive **Linux** native suite gate; Windows and full Chromium remain open.
- **Legacy test hardening implemented:** bounded listener readiness checks in Python relay tests; authoritative SSE frame/state barriers for undock, docking, cargo, and hangar operations; deterministic saved-session agriculture harvest rather than fixed 240-ms sleeps. Earlier sporadic relay and life-cycle failures are retained as regression evidence. Future stress and different-host validation remain worthwhile.
- **Non-capabilities:** No autonomous conversation, rumor transmission, trust changes, omniscient group knowledge, NPC travel, remote communications, or large-scale faction beliefs are created by encounters. FNV-based integrity is not a cryptographic security boundary.
- **Next GRSM-03c.3 design gate:** host-authorized social encounter effects and relationship deltas only upon causally grounded interaction events; prevent proximity from directly granting trust or changing canonical history. Consider group-route contact topology and explicit route/communication proofs as separate work. Preserve sparse APS scaling and bounded scheduling.
- Preserve single-galaxy source isolation from EVEmu archival audit, Lu, and Cognitum. Continue local-first development and use GitHub for source and milestone evidence.


## 2026-10-31 update: GRSM-03c.3 Linux milestone and next causal gate

- **Latest working baseline:** GRSM-03c.3, SQLite v12 with append-only host-attested directional rapport from explicit same-tick real NPC encounters. Existing trust, group knowledge, casualties, goods and history are not automatically altered. No autonomous social decision or intercolony communications implemented.
- **Verified local Linux:** full native/integration CTest **62/62**, Node.js **84/84**, Python design **3/3**, and prior troublemaking parallel relay group **12/12** over three consecutive rounds. New relationship gate contains two scenarios and survives reload/recover-copy, v11 migration and corruption attacks. [Detailed report](../GRSM03c3_Directional_Rapport_Report_2026-10-31.md).
- **Causality limitation:** The host-supplied assistance/harm classification has no independently derived physical action receipt. Co-location proves an eligible meeting, not assistance, attack, conversation, consent or faction-wide judgment.
- **Next GRSM-03c.4 engineering priority:** Introduce typed, physically committed, independently checkable interaction effect receipts (actor, beneficiary, source transaction, jurisdiction/context), then attach bounded individual interpretation and update directed rapport only when evidence is valid. Add negative tests for forged actions, false beneficiaries, replay, actor mismatch and migration. Keep mixed reputation, source credibility and legal opinions distinct.
- **Farther gates:** personality-guided decisions, group deliberation and leadership, verifiable communications/travel channels, APS cohort summaries and scale tests, Windows-native and visual-client end-to-end acceptance.
- GitHub stays a source/documentation mirror; local build/test is authoritative for this Linux gate. No EVEmu, Lu or Cognitum runtime changes.


## 2026-10-31 update: GRSM-03d member-level intergroup perceptions accepted

- **Current tested baseline:** GRSM-03d SQLite v13, explicit provenance-linked individual evaluations of encounters attributed to other organizations. Different members can hold mutually opposed views; the same NPC can hold different opinions through overlapping memberships. Group A's personal views of Group B need not match B's of A.
- **Verified local Linux full acceptance:** 63/63 native/integration tests, 84/84 Node, 3/3 Python design. An initial 62/63 failure came from an invalid historical v1 fixture that failed to drop new v13 attribution tables, not a behavior failure. The fixture was corrected and all 63 tests rerun successfully. Both logs are retained in [GRSM-03d evidence](../GRSM03d_Intergroup_Perspectives_Report_2026-10-31.md).
- **Authority boundaries:** A descriptive distribution is NOT an official organizational vote. Stance labels remain explicit host-supplied interpretations linked to real GRSM-03c.3 social encounter reactions, not independently validated physical aid/harm. No autonomous gossip or knowledge broadcast.
- **Design recorded:** [Nested loyalties and macro-group policy](GRSM03d_Group_Opinions_And_Nested_Loyalties_2026-10-31.md) separates individual feelings, descriptive member distributions, formal group charters/policies and federation-level negotiated decisions.
- **Next concrete gates:** (1) independently committed physical action receipts for aid, harm, rescue or theft; (2) individual context-dependent affiliation/loyalty profiles without one fixed loyalty bar; (3) ratified group positions/contested leadership and explicit temporal parent-group edges; (4) real travel/communications proofs, cohort scalability and Windows-native/interactive verification.
- Preserve local-first testing; GitHub is source and milestone record; never import EVEmu runtime or Lu/Cognitum modules into this game implicitly.


## 2026-10-31 update: GRSM-04a physical evidence appraisals passed

- **Current verified Linux baseline:** SQLite v14 direct-physical-incident appraisals, distinct from witness claims, subjective source trust, dyadic rapport and intergroup policy. A physical source must exist and be referenced by an individual **same-tick local-observation receipt**; secondhand claims cannot be promoted to physical evidence.
- **Measured tests:** 64/64 full native/integration CTest, 84/84 Node, 3/3 design, 11/11 persistence/GRSM suites; targeted triple repeats pass. An earlier unbounded full CTest was interrupted after partial progress; this failed-to-finish trace is retained, not counted. [Engineering acceptance report](../GRSM04a_Physical_Evidence_Appraisals_Report_2026-10-31.md).
- **Provenance and safety:** append-only v14 appraisal + epoch receipts, recovery-copy, non-member and remote observation denial, original first-person witness requirement, no retrospective attitudes, no physical-state mutation and negative corrupted-DB tests. This is a bounded local prototype, not cryptographic tamper proof.
- **Next GRSM-04b:** verify individual responsibility for real game actions using *actor-specific, committed* command or job-effect receipts. Separate actor, beneficiary, victim and eyewitness and forbid group blame without evidence. Only then allow bounded personality/loyalty-driven consequences.
- **Beyond that:** multi-dimensional loyalties, institutionally ratified positions, nested parent-group authority, communication topology, player-facing UI, real emergent NPC decisions, scale testing and native Windows acceptance remain open.


## 2026-10-31 update: GRSM-04b.A verified victim and observer role receipts

- **Latest local Linux prototype:** SQLite v15, append-only host-attested receipts for a named NPC casualty or a direct first-person physical observer. Every restored receipt reconciles to a committed physical incident, and every observer receipt to firsthand non-hearsay knowledge. Old v14 saves gain no retrospective roles.
- **Results:** full final 65/65 native/integration CTest, Node 84/84, design 3/3, targeted persistence/social 12/12. An interrupted first all-suite attempt is archived separately. [Full acceptance, failure provenance, negative tests and limitations](../GRSM04bA_Verified_Roles_Report_2026-10-31.md).
- **No unwarranted accusation:** ship, station, employer, guild, passenger or roster association cannot establish individual NPC culpability. Do not let a subjective appraisal promote a guessed perpetrator to a physical fact. The v15 API deliberately offers no perpetrator/beneficiary role.
- **Remaining GRSM-04b.B:** executable actor-specific NPC job/effect receipts (worker ID, signed or authoritative command/task identity, simulation tick, measured effect, owner/beneficiary/victim identities, journal validation/replay prevention) committed atomically with the real simulation effect. Actor role attribution only if independent source proves it, not if the host guesses.
- **Subsequent gates:** separate initiator/executor/beneficiary/witness; defensible social reaction and legal standing distinct from official group decisions; nested memberships/loyalty pressures; realistic communication routes; Windows/manual visual testing and population scale.
- Preserve local-first development and original Single Galaxy isolation from Lu, Cognitum and the EVEmu archival audit. This is an **accepted 04b.A gate**, not all of GRSM-04b.


## 2026-10-31 update: GRSM-04b.B1 verified named mining work

- **Current Linux baseline:** GRSM-04b.B1 / SQLite **v16**. A host-authorized specific onboard worker can receive an immutable mining-work assignment; only an **applied, physically committed ore transfer** yields a named execution receipt. Assignment without execution is not credit, nor is unrelated co-location, shared ship or group membership.
- **Verified acceptance:** full native/integration CTest **66/66**, Node **84/84**, design **3/3**, targeted persistence/social **13/13**. Early whole-suite runs were interrupted while legacy live relays ran; the final complete, bounded run passed. [Technical report and test logs](../GRSM04bB1_Named_Mining_Execution_Report_2026-10-31.md).
- **Evidence model:** acceptance in operation journal + named worker/ship/source target assignment + applied command receipt + conserved resource ledger and CargoMovement incident; all consistent at recovery. New assignment/execution tables are append-only with digest chains and noninventing v15 migration. These checks are not cryptographically authenticated multiplayer proof.
- **Scope limitations:** host-scheduled work isn't autonomous NPC labor; the model doesn't distinguish commanders, helpers, beneficiaries or victims for arbitrary mining/repair/combat. A valid ship action cannot automatically identify a perpetrator, even if it provides a named mining executor receipt.
- **Next GRSM-04b.B2 priority:** reusable actor-bound job outcomes for at least one additional physical labor path, separating commander, assigned worker and actual effect; reject ineligible workers, forgery, action failure, replay, stale location and source mismatch. Maintain role-specific consent/authorization and no unearned reputation transfer.
- **Defer multiplayer/launcher:** [NET-01](NET01_Deferred_Binary_Transport_And_Dual_Mode_Launcher_2026-10-31.md) records compact ID-based binary messages, invisible local host startup, optional Server Manager GUI, independent headless mode and single installer as *future* requirements. Do not implement them during GRSM work unless explicitly prioritized.
- Maintain the original Single Galaxy architecture separate from Lu, Cognitum and EVEmu research. GitHub remains source/documentation storage; testing is local-first.


## 2026-10-31 audit: ECON-01 practical supply chains and trades

- [ECON-01 foundational value-chain, skills/trades and management audit](ECON01_Foundational_Value_Chains_Trades_Management_Audit_2026-10-31.md) distinguishes tested mining/processing/farming/facilities/local hauling from missing professional equipment, scheduling, automated haulage, wages, markets and independent management.
- Continue **GRSM-04b.B2** as the next *concrete code gate*: effect-proven named fabrication or equipment repair, with explicit worksite, input goods and responsible actor receipt, not inferred culpability. It also serves as **ECON-01A** and the starting point for reusable occupation capability tags and work orders.
- After B2, build a complete small repair-and-resupply loop before broadening to many professions, player-founded markets, or simulated corporate politics. Preserve no-infrastructure genesis and no mandatory daily management.


## 2026-10-31 update: ECON-02A workshop, cross-skill trades and volunteer-board concept

- **Executable domain prototype published:** [ECON-02A universal workshop and labor board](ECON02A_Universal_Workshop_Labor_Board_and_Skill_Convergence_2026-10-31.md). A configurable workshop processes seven sample recipes through a local stock and one active task/worker. Entry-level work has no proficiency gate; advanced assembly requires two separate skills. A shortage can propose but cannot approve or execute a work order. Worker presence, power, inputs, output and cancelled reservation refunds are tested.
- **Verification qualification:** full native Linux build, 1/1 new economy test, 42/42 non-live native CTest, 84/84 Node.js and 3/3 design passed. A combined 67-test CTest run stopped amid older live-relay scenarios after 32 completed; full suite is **open** and cannot be marked 67/67.
- **Authoritative-game distinction:** GRSM-04b.B1 / SQLite v16 remains the latest verified durable physical actor-execution gate. The ECON-02A workshop is presently in-memory and is not synchronized to PersistentWorld's actual local goods, journal, social event evidence, user interface, or save files. Do not offer it as a playable factory until the link is completed.
- **Next direct integration gate: ECON-02B / GRSM-04b.B2.** Link a configurable workshop to a player-owned physical outpost or colony ship, reserve actual authoritative input materials, enforce available individual workers, consume real energy/time, commit output and named labor proof together, validate recover-copy and anti-tamper, and preserve resource conservation. Include at least one production/repair path beyond named mining, not a new artificial machine tier.
- **Labor path:** volunteered human assignment -> optional approved shortage proposals -> formal work board -> labor division -> labor department. Governance, salaries, general autonomous NPC task selection, inter-site hauling, business formation and wages remain future work. No organization unlocks magical worker capacity or stock.
- **Currency naming research:** [ECON-02A cross-country slang and provisional 'chits' proposal](ECON02A_Global_Money_Slang_Currency_Name_Research_2026-10-31.md). No official galaxy-wide currency has been approved or minted. Barter and local redeemable claims may precede local institutions; real monetary integration requires authorized issuer/ownership accounting and goods-backed settlement.
- **Important economic missing link:** conserved ship fuel/energy provisioning and verified business operating expenses. Existing movement does not prove a working fuel economy.
- Keep NET-01 binary network transport, server GUI and single-click packaging deferred as requested. Continue Single Galaxy local-first; Github remains source/milestone record.
