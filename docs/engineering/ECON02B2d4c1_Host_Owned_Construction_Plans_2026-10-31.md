# ECON-02B2d4c1 — Host-owned factory blueprint registration and durable cancellation

Date: 2026-10-31 / 2026-11-01 local continuation  
Baseline: `481fa56586fffc8e51da0cabab764ae080fc88d9` (ECON-02B2d4b)  
Status: Native host-plan service implemented and tested locally; **browser-to-live-host network adapter, graphical receipts, NPC builders and physical walls remain open.**

## Motivation and authority

The offline Factory Designer exported a validated `.sglayout`; the existing `BlueprintConstruction` helper could compile that design into an ordered, resource-backed series of game commands. Before this increment, the document itself carried a mutable cancelled flag, and the caller had to invent command IDs. That is unsuitable for a live construction UI.

New `HostBlueprintRegistry` is a **trusted native host service**. Browser/GUI documents are data, never permission grants. A host session supplies authenticated `owner`, `ship` and `site` separately from the design file. The world verifies that the player owns the colony ship and that ship created the outpost. The service accepts only a canonical `.sglayout`, compiles the existing restricted construction grammar, and checks the blueprint against actual world installations before registration. It rejects `.sgfactory` design overlays as executable plans; walls/doors/floors are not yet material-backed installations.

## Durable lifecycle and construction rules

- Registers an immutable canonical construction manifest in SQLite `sg_host_plans`; the host issues a new plan ID and checks there is no other **active unfinished** plan for the same outpost.
- Appends `register` and `cancel` events to `sg_host_plan_events`. Cancellation changes a durable host flag and must agree with the event sequence. Repeating an already committed cancellation is idempotent. A cancelled plan may never issue another job, including after reopening the world database.
- On status and execution, compiles the stored immutable plan, verifies source identity and a stable integrity checksum, validates its host-issued reference ID and event history, and checks current ownership and actual built equipment. In-progress or complete counts are **reconstructed from world state**, never trusted to a browser-supplied percentage.
- Issues **one** existing `BlueprintConstruction::execute_next(...)` job per authenticated host call, with a distinct internally allocated signed-range command ID. Allocation advances a durable `sg_host_command_seq` before command submission; interruptions can create harmless ID gaps but cannot reuse a previous ID under normal host operation. An unsuccessful construction attempt is a real journaled outcome and does not spend materials; a retry gets another command ID.
- Uses ordinary `PersistentWorld` submit/step and its SQLite effects and material accounting. The registry cannot spawn inventory, circumvent NPC/power/machine constraints, refund completed construction or erase already installed conveyor cells.
- A cancelled plan's partially built equipment persists. An owner can register a **new** immutable plan revision using the same geometry, but only after the previous plan is cancelled or finished. The successor recognizes the existing physical work and does not bill for it again.
- Uses the same authoritative SQLite database file, adding `sg_host_plans`, `sg_host_plan_events`, and `sg_host_command_seq`; existing simulation snapshot schema remains **v30**, unchanged. Existing save databases gain these tables upon first host-registry use.

## Scope and security limitations

The registry is a native host-side library, **not a remotely exposed server or live game GUI endpoint**. A real session authenticator must supply owner/ship/site; the raw SQLite path and registry entrypoints must never be made callable directly by browser JavaScript. The service requires the trusted host to bind it to the matching `PersistentWorld` database, serialize world mutations with plan mutations, and guard the reserved host command-ID range at the network ingress. Calls are serialized among registry instances **inside one process**, but no distributed cross-process leader/lease or full multiplayer authorization transport exists yet.

The stored checksum is a stable non-cryptographic corruption detector; it is **not a signature or substitute for OS/database access protection**. A malicious process that has write access to the host's SQLite database is outside this trust model. The service checks immutable content, owner/site/ship identity, plan reference IDs, and register/cancel event order; database-table access must remain host-only. No promise of database tamper-proofing against an administrator with arbitrary SQL write privileges.

Host command ID allocation is committed separately from the authoritative world receipt. This is crash-tolerant for no-reuse (gaps are allowed), but all job execution must go through one serialized authoritative world host; exposing the raw `PersistentWorld::submit` command-ID namespace to untrusted clients requires an additional admission rule.

## Local verification

- Linux all-target Debug CMake compilation: **PASS**.
- New native test `galaxy_econ02b2d4c1_host_plan_tests`: **PASS**. It imports the **exact browser-export fixture** `ECON02B2d4b_editor_blueprint.sglayout`, rejects `.sgfactory` as a host-executable plan, rejects spoofed owner/ship/site and duplicate active plans, executes actual module-backed construction, verifies monotonically increasing host command IDs, closes/reopens SQLite and resumes from world state, handles a real material shortage through subsequent mining/refining/module fabrication, durably cancels an active plan, creates a successor that reuses already-installed work, completes the real conveyor topology, checks material conservation, and rejects direct unaudited plan-manifest and cancellation-field edits.
- Non-live native CTest regression: **60/60 PASS**.
- Node.js browser/contract tests: **93/93 PASS**.
- Python design tests: **3/3 PASS**.
- Attempted full native CTest run did **not** complete within the session's execution window due to older live/relay scenarios; **not claimed as a full-suite pass**.
- Windows browser/native-host integration, multiplayer concurrency, and interactive construction from a running colony remain unverified.

## Next gates

1. B2d4c2: trusted live host application API transport with player session authentication, plan list/submit/cancel/execute/status receipts and game UI binding; do **not** interpret import as authorization.
2. Authoritative resource-backed wall, door, floor and structural placement, honoring design snap points; no simulated airtightness until separately specified.
3. NPC crew construction jobs and maintenance scheduling, with actual on-site workers, delivery paths, interruptions and audit receipts.
4. Independent Windows host/editor acceptance; live multi-user race and cross-process ownership/lease work as necessary.

GitHub is a repository and milestone archive, not the development hub. All work is built and tested locally first.
