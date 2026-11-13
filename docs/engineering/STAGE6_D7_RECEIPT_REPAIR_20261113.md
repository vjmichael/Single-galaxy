# S6-D7 — exact command-receipt repair (2026-11-13)

## Original v123+ station-mineral custody integration (2026-11-13)

The original simulation source proves that v123+ geological `Mine` creates tagged physical ore lots rather than the pre-v123 direct ship-mineral credit. Therefore the v127 D7 test's legacy `RefineMineral(12000kg plagioclase)` could not be authorized from newly mined silicate. Source now tests a real existing `BuildStationRefinery` with original modules, technicians and power, original docking/unloading of 30 independently mined tonnes, 30 tagged `RefineStationOre` batches with cooldown, original 12000kg plagioclase inventory and an explicit owner-requested station-source `RefineMineral` operation. The source-mode flag `thrust_x=1` requires a physically docked owner ship, its own commissioned refinery and the registered player at that station; it debits that refinery once before crediting ship goods, never silently consumes unrelated inventory. Legacy `thrust_x=0` ship mineral processing is unchanged. The fitting station is placed outside the original new-outpost 2.5m exclusion radius.

**Verification:** GCC C++20 syntax-only checks of the modified C++ fixture and simulation each passed locally; native linking/CTest and original material conservation/recovery are still pending. This is not Stage 6 acceptance.

## Native-run reconciliation — 2026-11-13

- Original Windows/Linux Actions run [#53](https://github.com/vjmichael/Single-galaxy/actions/runs/43455824013), **same source SHA `64f3c6a9c584c72dc058baabf6d759fe09e16c4d`**, both compiled; native acceptance was **38/39 on each OS**, with only `galaxy_stage06d7_positive_geological_freight` failing.
- The original Linux `stage06g-native-acceptance.json` captured **command 1, tick 1, outcome 5 (`InvalidArgument`)**, before the positive freight flow. The source-level verifier was working; the actual native `Simulation::execute(Action::Mine)` at snapshot >=126 refuses amount !=1, whereas fixture still requested 80 tonnes in one call. The original Windows artifact independently shows the same command/tick/outcome.
- First repair commit [`b1272a2`](https://github.com/vjmichael/Single-galaxy/commit/b1272a298d118fc728631fad3f674d5dffba2b0c) supplies a finite starter drill cost, installs/powers the real mine through the docked-v126 commands, registers the onboard owner, and executes 80 + 30 **individual one-tonne** physical `Mine` requests. Existing alternative legacy fixture path is unchanged.
- Isolated source compilation syntax check succeeded with GCC C++20; the first full local target build exceeded the execution environment time limit and **is not a native PASS**. Native Windows/Linux CI for the corrected original world is **pending**, and the downstream original mineral-industry path has not yet received a positive-run receipt.
- The independent `galaxy_stage06d7_receipt_contract` test is now required by `Stage06GNativeAcceptanceBatch.py`, not merely registered. Keep the entire matrix strict and preserve actual 1,000kg freight, rollback, conservation and recovery assertions.
- **D7 OPEN; G OPEN; no installation, merge or release authorized.** Do not repeat physical Radeon GPU validation merely because this original C++/SQLite test is failing.


## Objective
Keep original finite-source mineral extraction -> legitimately landed shuttle loading -> physical ascent -> original colony-ship unload -> mass-conserving independent SQLite replay. Do not loosen original inventory, workforce, owner, flight, journal, or process custody rules.

## Demonstrated source defect and repair
The prior `tests/WO3PlanetaryUtilityTests.cpp` harness used `world().events().back()` to equate the final global event with the just-submitted command. Original native simulation steps may emit additional unrelated events, making that assertion inappropriate for exact command proof. The source also contained an extra quote on the `GalacticSystemRegistry.hpp` include.

At source commit `9451a1714d68dd11de271c597538798da97dc108`:
- `step_applied` captures the pre-step event count and expected native tick.
- `require_applied_receipt` examines **only newly committed events**, matches command ID and tick, and demands **exactly one** matching-ID event with outcome `Applied`.
- Stale new events for the same ID, absent ID, duplicate ID, a rejection, and mixed Applied/rejected outcomes all refuse. A historical event outside the new range does not stand in for a new receipt.
- A diagnostic lists expected command/tick, matching count, and each newly observed ID, actual tick and named+numeric result.
- The malformed include was corrected, and direct last-global-event assertions in this fixture were replaced with the exact receipt check.

## Verification at source-review time
- `CMakeLists.txt` registers `galaxy_stage06d7_receipt_contract`, executing the same target with `--receipt-contract`.
- Adversarial fixture has **10** cases: valid receipt, valid receipt followed by unrelated event, valid with prior historical event, missing, wrong command, wrong tick, refusal, duplicate Applied, mixed Applied/refused, stale same-ID alongside Applied.
- An **isolated standalone Linux C++17 compilation of the copied verifier logic** passed all 10 contract cases with `-Wall -Wextra -Werror -pedantic`. This is a diagnostic compile only, **not** an exact-head native `galaxy_kernel` or CTest run.
- Source and CMake consistency were re-read through the connected GitHub repository. The test fixture no longer uses `events().back()`.

## Open mandatory acceptance
**DO NOT mark D7 closed.** The previously reported native Windows and Linux Stage 6 matrices passed 38/39 tests each, with `galaxy_stage06d7_positive_geological_freight` failing; original full job logs were not retrievable in this repair session, so the exact root cause is not proved by a new execution.

On the final integrated source head, run the real Linux CMake build and:
```sh
ctest --test-dir build --output-on-failure -R '^galaxy_stage06d7_(receipt_contract|positive_geological_freight)$'
```
If failure persists, inspect original failing command ID, tick and named outcome from the new diagnostic. Repair only demonstrated defects, then run consolidated Windows/Linux native acceptance at **one identical SHA** and reconcile conservation, rollback and restart receipts. Installed Godot acceptance remains a separate later gate and does not block feature development.

## Sequence / status
1. D7 source verifier and adversarial contract: **implemented; isolated verifier compiled/passed; exact native run pending**.
2. E6 actual staffed ecological production: previous work exists; reconcile at final head.
3. F genuine planetary named-NPC movement/survival: previous work exists; reconcile at final head.
4. G independent multi-cell/process custody, crash/restart, and one-SHA Windows/Linux acceptance: **open**.
5. Installed Windows/Godot acceptance: **separate**, defer repeated physical Radeon GPU validation until needed for final release.

No installation or Stage 7 authorization is implied by this source-only change.
