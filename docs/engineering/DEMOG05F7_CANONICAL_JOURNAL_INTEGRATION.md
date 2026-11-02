# DEMOG-05f7 — Canonical journal integration specification

## Verified code paths
- `PersistentWorld::submit` clones `Simulation`, queues a `Command`, then commits with a **non-null command pointer**, publishing memory only after database commit.
- `PersistentWorld::step` commits with a **null command pointer** and advances `Simulation::tick()`.
- The experimental `submit_testing_npc_identity_relocation` in 05f5 uses a null command pointer **without advancing tick**, yielding duplicate historical step generations on reopen.
- `Command` currently carries scalar arguments only, not an NPC manifest.
- `Simulation::submit` rejects unrecognized host-authority actions. Adding a new action requires snapshot action validation and historical replay updates.

## Required implementation sequence
1. Define a versioned `HostTransferPassengers` command and durable canonical manifest keyed by stable command ID. Do not overload `HostCrewLoss` or pretend a passenger transfer is a world step.
2. Extend command serialization/deserialization, snapshot version, command replay and audit validation together.
3. In `Simulation`, validate unique existing NPC IDs, exact source custody, compatible ship state, physical seats, and current destination. Keep NPC ID, home, role, XP, mentor, and faction references unchanged.
4. Apply source/destination aggregate headcount and named-NPC location changes as one deterministic simulation effect; ensure world conservation checks still pass.
5. Persist operation, manifest, effect receipt and snapshot in one transaction. Publish in-memory world only after commit; duplicate ID must replay idempotently, not relocate twice.
6. Test before-commit and after-commit failures, reopen, replay, concurrent reservations, destination destruction, cross-region and family continuity.

## Explicitly rejected shortcut
Calling `store_->commit(..., nullptr, ...)` for a transfer is not safe. Neither is writing extension SQLite passenger tables separately from the canonical world journal.

**Status:** 05f5 is a failing experiment; 05f7 is an integration design, not a successful physical-transfer implementation.