# FP3D-02E — Native journal-proofed interior steps and Windows smoke repair

Baseline: Single-Galaxy PR #107 (`sg/fp3d02c-authoritative-interior-movement-20261108`), stacked on PR #106.

## User evidence inspected

- `SingleGalaxy_Windows_Build_Evidence.zip`: Official Godot 4.7.2 editor and template SHA-256 checks **PASS**; engine startup and project import **PASS**. Headless runtime-smoke **FAIL** (`player/camera was not created`); Windows game export was not reached. Original logs available in the attached user ZIP.
- `SingleGalaxy_NativeBridge_Windows_Evidence.zip`: Native observer Windows runtime **PASS** for original ticks `0,1,2,3`, SQLite restart ticks `3,4,5`, no stderr; this is a real Windows process result, **not** a Godot F7-client end-to-end pass.

## Code repairs

- Godot `godot_smoke.gd`: validate an attached `Camera3D` by actual type rather than assuming an engine-generated child node has the readable path `Camera3D`. Separate missing-player from missing-camera failure messages. The user's visible FPS render confirms a first-person camera can exist; **this repaired smoke script has not yet run on Windows Godot**.
- `NativeInteriorMovement`: every accepted movement receipt must be backed by an original authoritative `operation_log` `kind=2` STEP with exact generation and tick and nonempty original state hash. When the corresponding `snapshots` generation remains (snapshot compaction otherwise removes older rows), the state hash must match that snapshot's checksum; presence of a discarded snapshot is **not** required.
- Sqlite failures other than duplicate unique/primary-key receipts propagate rather than appearing as ordinary rejected commands.
- Two testing-only fault injections after receipt insertion and after pose update exercise rollback via `BEGIN IMMEDIATE ... ROLLBACK`; no half-committed movement is allowed. Native tests also temporarily alter an operation to non-STEP to demonstrate a corrupt journaling claim is rejected, then restore and verify the original world.

## Execution evidence

Native Linux compile and process test: **PASS** using actual R5 kernel object archive and SQLite. `test-results/native-test.log` contains the exact output.

## Important remaining gates

This is a **journal-anchored host-only movement prototype**: its pose and receipt live in a separate SQLite transaction from the native world tick. It has NOT been integrated into the original `PersistentWorld` command/operation atomically. It has NO Godot write ingress, verified user session, authoritative boarding, full-world XYZ, or physical Windows gameplay acceptance. Original 67 failing regression tests remain unresolved. The Godot smoke test fix is source-level only until actual Windows execution.
