# DEMOG-05q6e1 — Crash-safe guardian/dependent registered residence (v41e)

**Date:** November 2, 2026  
**Project:** Original Single-Galaxy simulator, not EVEmu server  
**Baseline:** Locally verified DEMOG-05q6d (v41) focused C++17/SQLite source  
**Status:** Locally implemented, tested, and packaged. **This is not a merged main-game kernel, GUI change, or Windows build. GitHub is the source/documentation repository, not the development environment.**

## What this milestone verifies

- Extends the privileged adult `SocietyMigrationPlan` with a bounded, ordered, duplicate-free manifest of up to 32 reserved ART02 dependent child IDs and independent immutable move-event IDs. The **adult guardian** has a real completed passenger voyage and disembarkation before any home change is allowed.
- ART02 validates that the manifest exhausts every dependent attached to the guardian in the origin settlement, that parentage is genuine and nobody is dead/duplicated/moved earlier. It commits guardian + child **registered residences** as separate immutable family move receipts with census-revision compare-and-swap.
- ECON03 applies `-household_size` to origin and `+household_size` to destination under a stable **Cold** owner generation and matching checkpoint revision. The canonical world adult NPC/crew count changes by exactly one; high-range child identities do **not** become playable NPCs or staff.
- The world snapshot/step, adult migration receipt, family move events, dependents' linked custody receipts and regional population changes all commit in one trusted SQLite transaction. Injected failures roll everything back; retry does not duplicate a child or Bob.
- `sg_society_migration.household_count` is a backward-compatible additive field, defaulting to 1 for previous adult migrations; `sg_society_household_member` uses scoped event links and permits the same child to move again on a separate journey. Reopen rejects missing/tampered child moves, forged household counts, stale region ownership, incomplete manifest or duplicate receipts.
- Read-only medical/society reconciliation counts dependent census identities by their **current** ART02 residence, preserving their immutable birth settlement, parentage and appearance history.

## Verified local tests

| Verification | Result |
|---|---|
| Strict C++17 Debug CTest | **21/21 passed** |
| C++17 Release (-O1) CTest | **21/21 passed** |
| UBSan (Debug), new household test | **1/1 passed**; full inherited UBSan suite not rerun |
| Final source ZIP checksum verification | **117/117 files matched** |
| Final ZIP clean extraction + fresh Release build + new household CTest | **1/1 passed** |
| Previously inherited native regressions | **20/20 passed** in Debug and Release |

New test sets up two real ART02 dependent births, a real parent NPC (Bob), real ferry boarding/gate traversal/disembarkation, and atomic immigration. Before the move, home A has 8 registered residents (6 named adults + 2 dependents), home B has 6. After the guardian and two dependents move, A has 5 and B has 9, global population remains 14 registered, and live named worker counts change by 1 only. It checks bad child lists, stale owner, failed commit then retry, restart, independent genesis replay, forged family residence and missing/downgraded household receipts.

**Deliverable:** `SingleGalaxy_DEMOG05q6e1_Atomic_Guardian_Dependent_v41e.zip` — 401897 bytes. SHA-256: `53b305bb43579cf1e74449e340abdb2d1ec71d945d1d0efee3e480848818ec99`. Includes full focused CMake source, the v41→v41e patch, test source, build/test logs, engineering report and file integrity manifest.

## Crucial limitation: this is not full household *transport*

The ART02 reserved children **do not yet have physical NPC entity records or berth/boarding/in-flight/arrival manifest entries**. The trusted event moves their **registered residence after the guardian's documented trip**. Do not infer or claim that the children physically occupied the ferry or survived a mid-flight crash as separate physical passengers. Full DEMOG-05q6e acceptance still requires explicit dependent passenger custody while boarding and travelling, constrained berths, crash recovery at every transition, optional second-parent travel, household admission/housing policy, and collision-safe child movement across both cold/warm regional owners.

The full game source, production workforce/household UI, Linux→Windows acceptance, and cold/warm handoff integration are not yet merged or validated. Full police/fire/public EMS institutions remain deferred by user instruction.

**Next:** DEMOG-05q6e2: durable child boarding and travel custody linked to actual transport ID, physical berth admission, per-phase crash/restart, and two-parent/whole-household immigration settlement.