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
