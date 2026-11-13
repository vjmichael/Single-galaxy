# S6-D7 — exact command-receipt repair (2026-11-13)

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
