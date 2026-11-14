# Single-Galaxy master task ledger — authoritative priorities

**Owner-directed priority reset: 2026-11-13 (America/Phoenix).** This file on `main` is the ONE priority order for EvEmu / Single-Galaxy. Branch-specific milestone backlogs (including `docs/engineering/FEATURE_FIRST_BACKLOG_20261113.md` in Stage 7) are subordinate execution history, not competing master priorities. The binding acceptance policy remains [CI_TASK_GROUP_ACCEPTANCE_POLICY.md](CI_TASK_GROUP_ACCEPTANCE_POLICY.md). The exact 114-PR snapshot is [PR_CONVERGENCE_INVENTORY_20261113.tsv](PR_CONVERGENCE_INVENTORY_20261113.tsv).

## Non-negotiable operating rules

1. **No new branches or PRs for feature tasks, bug repairs, subgroups, CI, or integration.** Reuse and reconcile the **existing** reviewed branch/PR histories. The intended destination is **one `main`**, not another stack. Changes to `main` must preserve reviewed code, source provenance, original native tests and historic SQLite migration paths. Do not force-push or silently discard an earlier development line.
2. **Batch engineering and batch acceptance.** Finish a defined integration/task group before requesting the **one end-of-group** exact-head Windows/Linux acceptance. Never trigger CI merely because a task or small bug fix was committed. During work, use static review, bounded local compiler and targeted local tests. For already running checks, collect useful evidence but do not reflexively launch successors. After failure, collect findings, implement related corrections together, then test the relevant affected group; do not spin a workflow per correction.
3. **Preserve and enforce the repository's existing November 9 CI policy.** On any legacy branch whose workflow still has automatic `push` or `pull_request` triggers, use `[skip ci]` on interim source commits and avoid introducing more automatic task-level triggers. Trigger the approved consolidated `ci-task-group-acceptance.yml` workflow **only when a completed integration group is ready**, using its exact source SHA; never confuse queued, cancelled, stale or skipped checks with passes. Documentation-only priority updates are not acceptance events.
4. **No physical Radeon, Windows installer, SCM/service or repeated installed-gameplay gates** before the source backlog is integrated. Do not add artificial NPCs, resources, credits, shipment receipts, legal authority or source/test waivers to make a gate green.
5. **One ledger.** Update this ledger in the same batch that changes a group's scope/status. Do not open a tracker branch, PR, or new ledger for each discussion.

## P0 — #1 priority: repository source convergence and main-line integration

Snapshot: **114 open PRs, all draft; 0 target `main`**. These are **113 distinct open-PR head branches** and **three open-PR parent-chain roots** (81 PRs anchored at `integration/colony-auto04b-safety-host-v66-20261103`, 31 anchored at `integration/fp3d-group2-wo3wo5-20261110`, and 2 anchored at `sg/stage7-pop06-civic-policy-20261113`). A root here is the base of an open-PR chain, **not evidence that the underlying Git histories are unrelated**. Do not make 114 independent merges or launch their 114 old CI workflows.

As-of-audit refs: `main` = `443f31e31fe2f51fcdd0230451403eafdea211a7`; current Stage 7 POP-10 = `bace7988b5ecfb495a650a7c407d110a2e2b44e5` (draft PR #135 on #134 lineage); Godot Group 3 = PR #133 head `sg/group3-mining-industry-20261111`; earlier integrated Godot Group 2 = PR #130; Stage 6 planetary = PR #127. GitHub compare of `main` to the Stage7/Group3/planetary candidate heads reports `diverged` and common ancestor `b12e0e2e2278c17849f493b89288f9a725b8f233`. Stage7 was ~4,288 commits ahead of that ancestor and `main` had 136 independent later commits; GitHub's comparison file list is capped at 300 and **is not a full conflict/inventory audit**.

**Group INT-01 — dependency and source inventory [ACTIVE, first group, no acceptance CI].** Preserve every open PR source SHA, base dependency, changed files/unique commits, original status and independent receipts. Use the complete TSV snapshot to collapse the stacked DAG into logical owned components. Identify exact common ancestor, current `main` unique changes, missing ancestors outside open PRs and already-superseded branches. Do not close PRs from a count alone. No new integration branch.

**Group INT-02 — choose authoritative source and reconcile main-only changes [NEXT].** In the existing reviewed line, establish a deterministic file/domain ownership map for canonical kernel/SQLite, historical migrations, workers, native gameplay/Godot, and Stage7. Distinguish historic shared ancestor content from actual unique changes; audit the 136 main-only commits. Decide and document only the minimum merge/cherry-pick/content reconciliation necessary. Preserve all original native tests and main CI controls. Do not resolve conflicts by overwriting the later complete system with an older copy.

**Group INT-03 — converge core and historical native source [PENDING].** Reconcile canonical Simulation/Persistence/CMake, worker/process-custody, original Stage6 H1/H2/H3/H4 and WO1–WO5, historical v86–v135 original SQLite/restore tests and one-writer source authority onto `main` without new PRs.

**Group INT-04 — converge playable Godot, ship/station and planetary source [PENDING].** Reconcile old FP3D/Godot chain, Group2 integration, Group3 ship/fitting, VIS-01 and S6 planetary work against that same canonical kernel. Preserve genuine boarding, docking, movement, mining and save recovery. Review all asset provenance and avoid inventing physical Radeon results.

**Group INT-05 — converge Stage7 households/society/economy/justice [PENDING].** Bring the surviving reviewed POP-01–10 (existing draft PRs #134/#135), original resident identities, durable economies, authorized labor, two-way trade and due process into the converged source. Incomplete trade/justice features remain marked partial; don't call a proposal a physical execution or a review a criminal verdict.

**Group INT-06 — final consolidated acceptance and main closeout [PENDING, NOT RUN].** Once the agreed integration source is assembled and reviewed at **one unchanged SHA**, run one selected Windows/Linux source-bound native acceptance with original simulations, process custody, SQLite/history, law/shipping, Godot and failure recovery; investigate the grouped failures and reconcile source before final release/merge judgment. Use the November 9 gate mechanism, record artifact hashes and exact SHA, then retire/supersede reviewed old draft PRs with recorded source equivalence. Only after this group is accepted should release and owner-PC installation be reconsidered. A passing hosted runner is never physical Windows acceptance.

## P1 — remaining feature backlog, AFTER convergence is underway

- **POP-10B positive genuine reciprocal barter:** actual two-direction manufactured goods, independent original-source/carrier/receiver approvals, physical pickup, carrier travel/unload, bilateral settlement, original SQLite recovery. Source authorization exists but complete positive two-way native proof is open.
- **POP-11 original demand-driven autonomous production/economy:** NPC choice → real existing original equipment/labor/materials → manufactured physical inventory → transport and authenticated reciprocal trade. No phantom payments or factory.
- **POP-12 real monetary/compensation accounting (if approved by design):** conservation, funded balances, durable counterparty receipts and legitimate tax/settlement policy; existing physical v56/v57 shipment receipts do not imply minted currency.
- **POP-13 due-process governance:** authenticated original law agency, notice/answer and independent review; expanded legal procedures and enforcement only with separately justified jurisdiction, review, appeals and authorization.
- **POP-14 social integration:** household lifecycle, personal outcomes, scarcity and economics, group behavior and civic transitions in one durable NPC decision loop.
- **H4 residual historical original-world compatibility:** independently verified source-era restores and replay/tamper gates. Coordinate with INT-03 rather than spawning another branch.

**Acceptance cadence:** Groups INT-03, INT-04 and INT-05 each receive at most one planned Windows/Linux acceptance *after their integrated group is completed*, followed by a distinct final INT-06 consolidated run. Early targeted local compiler/test diagnostics are allowed without GitHub CI. If an end-of-group run exposes genuine defects, batch the related corrections before one affected-suite rerun; do not recreate a gate after every source edit.

## Group status / gate register

| Group | Source status | End-of-group acceptance | Release |
| --- | --- | --- | --- |
| INT-01 PR inventory and authority map | **Active**; 114-row snapshot recorded | **NOT REQUESTED (documentation/source audit group)** | No |
| INT-02 identify authoritative source and main-only deltas | Pending | Not requested | No |
| INT-03 original core, WO and H4 integration | Pending | End-of-group batch only; not requested | No |
| INT-04 Godot/planetary/station integration | Pending | End-of-group batch only; not requested | No |
| INT-05 Stage7 POP integration | Pending | End-of-group batch only; not requested | No |
| INT-06 consolidated native acceptance | Pending | **Only after preceding integration groups complete** | No |

**Current known partial native evidence (not final group acceptance):** Stage7 `bace798` exact-head Linux 19/20, law-agency original recovery failed with `recovery: cannot verify latest state; refusing repair`; Windows result still needs exact-run reconciliation. Earlier `66261c4` Linux 19/20 demonstrated the DOCK03 SQLite-cursor repair. These are diagnostics, **not a reason to trigger more CI now**. Existing Stage7 hourly native-closeout scheduled task was paused to prevent per-fix rerun churn.
