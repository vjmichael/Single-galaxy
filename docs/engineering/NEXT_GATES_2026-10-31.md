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
