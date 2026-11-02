# DEMOG-04b2c — Cross-authority fail-closed gate

**Status:** Engineering acceptance design; gameplay implementation NOT complete.

The existing DEMOG-04b2b read-only preflight compares a PersistentWorld NPC record with a separately seeded expedition ledger. The new acceptance gate must reject any NPC whose custody changes in the prototype without an equivalent committed world effect. A read-only check is a diagnostic, never an authorization token: another writer could change state immediately after a check.

## Required atomic protocol

1. The host resolves stable NPC IDs and canonical faction memberships, verifying current owner, assignment, ship, stock, source headcount and family records from the same authoritative world snapshot.
2. In one world-owned transaction, reserve all unique NPC IDs and ship/cargo and append a durable outbox/effect receipt with operation ID, source revision and destination. Reject stale revisions and conflicting active custody.
3. Transit, cancellation, loss and arrival consume the exact receipt once. Each operation is replay-safe and updates the canonical NPC owner and headcount, not a second independent ledger.
4. Faction rosters query canonical state and apply durable notification events in monotonically increasing NPC revision order. Event fan-out may lag, but no stale roster is permitted to authorize another boarding.
5. On crash/restart, replay incomplete world-owned effects, reconcile region ownership, and rehydrate roster projections. Do not grant passengers or cargo twice.

## Acceptance tests to implement

- Three factions reference the same Bob; competing board commands can result in only one winner.
- A valid read-only preflight followed by a conflicting write must fail at final commit (TOCTOU).
- Commit followed by crash before roster notification must reconcile all rosters on restart.
- Duplicate, delayed, dropped and reordered event delivery must not regress NPC status.
- Source and destination save/restart, ship destruction, cancellation, late arrival and interrupted cross-region handoff preserve exactly one custody owner and physical stocks.
- Named NPC records and aggregate cohorts reconcile without counting named residents twice.
- Family links, profession, skills and history follow the NPC identity without copying the person.
- A real ship and materials must exist and be debited exactly once.

**Stop condition:** No automatic NPC expeditions or playable-world migration until the world-owned transaction and these tests exist. The current isolated SQLite expedition prototype and read-only reconciliation are useful research but are not the production authority.
