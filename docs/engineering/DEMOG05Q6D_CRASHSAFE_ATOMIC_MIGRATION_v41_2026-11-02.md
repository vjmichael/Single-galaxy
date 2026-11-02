# DEMOG-05q6d — Crash-safe travel and atomic adult migration (v41)

**Date:** 2026-11-02
**Project:** Original Single-Galaxy C++17/SQLite simulator (not EVEmu server)
**Baseline:** Locally verified DEMOG-05q6c v40c
**Status:** **Implemented and tested in a standalone focused source package, not merged into the main game's kernel/client or Windows release.** GitHub serves as repository and engineering record, not development hub.

## Crash-recovery rule

Physical custody, active voyage and legal home are distinct authoritative facts. On a database crash, Bob resumes at his *last committed* phase. A failed boarding leaves him at origin; a committed departure leaves him aboard the ship; in-flight progress resumes from the last saved tick; computed but uncommitted arrival leaves him aboard; successful disembarkation does not itself change legal home. Exactly one NPC ID exists throughout.

## v41 implementation

1. Added `tests/crash_custody.cpp` testing actual boarding, gate travel, travel-progress steps, and disembarkation with SQLite failpoints after receipt/snapshot writes, reopen and independent journal recovery. Physical and home custody always agree with the committed state.
2. New privileged `Action::HostSettleMigrant`, journal codec and v41 snapshot: a named adult can migrate only after the recorded real ferry voyage has completed and disembarked at the destination. Existing v34-v40 save parsing/action ordinals remain compatible.
3. New `PersistentWorld::step_with_atomic_migration` executes exactly one due adult migration under world-generation, ART02 census revision and ECON03 stable-Cold-owner revision/generation fences; simulation snapshot, immutable family residence event, source/destination regional population moves and host effect receipt all write in **one SQLite transaction** and publish in-memory state only after commit. Ordinary `PersistentWorld::step` now refuses queued migrations, preventing a bypass of society effects.
4. ART02 `npc_family_moves` retains original lineage while allowing a new current registered residence; later family death and enrollment operations respect latest residence. Moving an adult adjusts both family census counts with CAS validation.
5. ECON03 real scheduler creates -1 origin/+1 destination population events and an immutable regional migration receipt. The combined colony population does not change.
6. Reopening cross-checks every applied migration against original and destination historical world snapshots, real voyage/arrival, the exact ART02 family move, ECON03 regional receipt and `sg_society_migration` effect. Forged/missing receipts, stale owner generations, mismatched mappings and illegal migrations fail closed.

## Local verification

| Check | Result |
|---|---|
| Strict GCC C++17 Release (-O1) | **20/20 CTest passed** |
| GCC Debug + UndefinedBehaviorSanitizer | **20/20 CTest passed** (two bounded groups: 11 and 9) |
| Previously inherited regressions | **18/18 preserved** |
| Final ZIP SHA256 file manifest | **108/108 matched** |
| Independent fresh extraction / rebuild / CTest | **20/20 passed** (two bounded groups: 14 and 6) |
| Crash points, rollback, restart, genesis replay, corruption rejection | **Passed** |

A concrete two-colony case starts with six residents at each and a real six-crew ferry. Bob travels through a real gate, disembarks without changing home, then migrates under an authorized committed action. Home-colony counts become **5 and 7**, total 12; Bob keeps his stable NPC ID. Rollbacks and replay do not duplicate him.

**Artifact:** `SingleGalaxy_DEMOG05q6d_CrashSafe_AtomicMigration_v41.zip`; SHA-256 `fb8562146d9262d3a2c8fcd23d28bca30a8200cf285fe6d920ea1ae277c11a29`. Includes full focused CMake source, 20 regressions, v40c→v41 source patch, engineering report, test evidence and integrity manifest.

## Explicit outstanding integration gates

- Only one adult, no dependent child or manifested household relocation. Farm/habitat staffing, medic/EMS crew links and other specialized job assignments cannot yet be migrated without separate atomic reassignment.
- Requires a common SQLite file and stable Cold regional owner, not active Warm promotion/demotion, distributed commits or fully integrated production system.
- Native Windows tests, real full-game player UI and GitHub-main source merge remain unverified/not performed.
- Full police, fire, and comprehensive EMS/public emergency institutions remain **deferred at user direction**.

**Next:** DEMOG-05q6e — real household migration with dependent census identities, manifest and custody conservation, admission policy, and cold/warm ownership transition tests.
