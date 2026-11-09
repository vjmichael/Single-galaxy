# CI task-group acceptance and queue custody

**Adopted 2026-11-09 by repository owner direction.**

## Binding rule

Run Windows/Linux GitHub Actions *acceptance gates at the end of each completed
task group*, not for every small implementation commit, source repair, formatting
change, or follow-up note. During the group, use bounded local compilation,
unit tests, static/source tests, and reviewable evidence. Do not claim these
substitute for independent end-of-group OS acceptance.

A group may request one exact-head native acceptance run when its implementation
is ready. The initial Windows/Linux run covers the relevant cross-component
suites and actual native fault and recovery behavior. If it fails, repair the
specific cause and rerun only the failed/affected suite at a new exact SHA. Do
not launch 60+ historical workflows merely because a PR contains CMakeLists.txt,
Simulation.cpp, or another shared implementation file.

## Trigger design

- Historical specialist workflow files are test recipes, not automatic
  per-commit PR/branch CI. Prefer **manual group-selected gates** via
  workflow_dispatch or a trusted owner-authorized explicit acceptance signal.
- Preserve build/test recipes and generated evidence; removing automatic PR
  triggers does not mean removing acceptance requirements.
- Ordinary commits, stacked-PR synchronize events, documentation edits, and
  partial work are **not** group acceptance events.
- Any future automatic CI is deliberately limited to inexpensive structural
  checks at an agreed task-group boundary or on protected-branch release.
- Before an acceptance run, record the task group, branch, exact head SHA,
  Windows and Linux targets, failure/recovery requirements, and artifact paths.
- Keep independent acceptance distinct from unit/source checks and from
  physical Windows GPU/render/camera playtesting.

## Interim commits and the one explicit group gate

For still-open historical PR branches whose legacy leaf workflows automatically
trigger on every synchronize event, use `[skip ci]` in **interim implementation
commit messages** until those old triggers have been retired. GitHub documents
that this suppresses `push`/`pull_request` workflow starts; it does not
suppress manually initiated or separate `push` requests on main. Do **not**
use a skip marker on the final `ci/task-group-request.json` update. Because
skipped required checks can remain Pending, do not merge on the strength of
skipped checks: use the designated accepted group gate and then satisfy any
required merge rules independently.

The on-demand workflow `.github/workflows/ci-task-group-acceptance.yml` is
the single exact-head entry point. At completion of a supported task group,
update only `ci/task-group-request.json` on `main` with a task-group ID,
the open PR number, and its exact current 40-hex source SHA. This triggers
one authenticated Linux/Windows acceptance workflow that refuses stale SHAs.
The initial group is SGDX03; configure and review other group recipes **at
their group boundaries**, not on every small source edit. Direct manual
`workflow_dispatch` is also available. A request is *not acceptance* until
the actual OS jobs and independent artifacts pass.

## Queue hygiene and cancellation

- Before an end-of-group gate, inspect queued/running runs. Cancel **queued**
  PR runs at commits that are no longer the latest SHA of their same-repository
  head branch, or runs explicitly superseded by a newer accepted group gate.
- Preserve latest-head group gate runs; never cancel actual running OS jobs
  without separately verifying they are obsolete. Avoid blind queue purges.
- Keep an auditable list of canceled run IDs, old and current SHAs, reason,
  timestamp, and API outcome. Retry failed cancel requests conservatively.
- The dedicated `.github/workflows/ci-queue-hygiene.yml` runs maintenance
  independently of game build/test CI. It is a bounded queued-only cleanup;
  it must **not** be represented as executed until an assigned runner completes.
- If GitHub runners are unavailable, report `queued/runner_id=0/steps=[]`
  instead of compiler failures or accepted gates.

## Operational sequence

1. Work on a task group without new acceptance jobs on each commit.
2. Finish implementation, integration, local checks, and review.
3. Select one relevant Windows/Linux workflow; run on exact SHA once.
4. Inspect actual OS compilation, runtime logs, minidumps/SQLite/visual
   artifacts; repair only genuine findings.
5. Record PASS/FAIL/BLOCKED per gate and archive verified evidence.
6. Cancel superseded queued PR runs and proceed to the next task group.

This rule is authoritative for Single-Galaxy development work. Apply the same
principle to other project workstreams when their CI is modified.
