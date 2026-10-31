# Engineering Handoff — 2026-10-31

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
