# COLONY-AUTO-04A — Life-support safety foundation (local v65)

**Date:** 2026-11-03. **Status:** Native source and tests implemented locally. **Only this engineering note is committed to GitHub at this point; the new C++ source is in the reproducible ZIP, not yet merged into the repo.** This is not full COLONY-AUTO-04 acceptance.

## Actual implemented code

- `include/galaxy/ColonySafety.hpp` and `src/ColonySafety.cpp` define a read-only risk assessor reading actual `Simulation` ship, farm and habitat compartments and using existing `macro_needs::runway` (not fictitious imported stocks).
- Distinguishes safe, forecast-shortage, critical and emergency states. Physical risk does not depend on social grievance, loyalty or anger. The signals `should_prepare_departure` and `emergency_override` are recommendations to a future emergency controller, **not** physical passenger movement or active player-command blocking yet.
- Owner-checking `preflight` permits the idea of disabling local production, with a safety warning when supplies are short, but refuses proposed orders to seal an occupied exit, cut off distress, dump atmosphere or forbid urgent repairs. No current production switch is altered; actual gameplay enforcement remains to be wired to the authenticated colony host.
- `record_decision` appends unique incident facts through a caller-provided canonical `sqlite3*` transaction. It creates an append-only table with update/delete rejection. Caller must integrate it into the **same** world-writer transaction. It has no additional open writer and makes no claim of cryptographic tamper-proofing. Facts distinguish the attempted order from proven malicious intent; GRSM per-NPC observation remains future work.
- The existing `ColdColonyNeeds` engine continues consuming physical food/water/oxygen even when no local production or import arrives. Test exercises actual zero-supply risk without deleting population or inventing ships.

## Executed native acceptance

- New `galaxy_colony_auto04a_safety` CTest **PASS**, including live four-crew stock 100→16→4→0 kg, owner authorization, danger thresholds, four prohibited occupied orders, real cold-cohort consumption and funded depot delivery, snapshot re-opened forecast, SQLite idempotence/immutability/rollback/reopen.
- Eight predecessor CTests **8/8 PASS**: `galaxy_kernel_tests`, `galaxy_persistence_tests`, `v59_multi_destination_host`, `galaxy_econ03c2coldcolonytests`, `galaxy_econ03c3regionalschedulertests`, `galaxy_econ03c7modeauthoritytests`, `galaxy_econ03c8realworldbridgetests`, `galaxy_econ03c8bworldreconciliationtests`.
- The colony live-host target builds against the extended kernel; not all its GUI transport tests were run.
- GCC UndefinedBehaviorSanitizer **PASS** for the new module/test (the prebuilt rest-of-kernel archive was not rebuilt with sanitizer). **Full game regression/Windows/browser/multiplayer acceptance not run.**

## Reproducible source

`SingleGalaxy_COLONY_AUTO_04A_SafetyCore_v65_2026-11-03.zip`: complete unified v64 game source plus 04A files, **714/714 source checksums verified**, 1,931,293 bytes, archive SHA-256 `c17e94c3c8f9ea1217b1819fb292e79a4ecf0e6c6852445942a59309aa816828`.

## Remaining core steps

1. Apply the C++ sources/tests/CMake integration to `main` under verified source-blob checks, then rerun CI/native tests.
2. Connect an explicit, persistently journaled production policy command and its safety guard to the authenticated colony host. No UI or player accessible production toggle exists from this module yet.
3. Implement genuine emergency labor refusal, supplies, feasible migration and evacuation via existing physically conserved people/ship/custody ledgers. No instant departures, deaths or stock conjuring.
4. Connect evidence to per-NPC observations, social grievance and GRSM, with historically plausible information propagation and no omniscient judgment.
5. Build a visual player warning view, regional cold/detailed recovery tests and installed Windows acceptance.

The existing policy against depicting harms to children remains binding, and the explicit dependent custody gate cannot be bypassed.
