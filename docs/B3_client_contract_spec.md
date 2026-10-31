# B3 — Client-facing data contract v1

## Scope

Original one-fictional-galaxy game; **one active star system** in this milestone. Local observing/presentation contract only, with no changes to EVEmu or the historical client. Deliberately preserves deterministic authoritative game rules inside `Simulation`/`PersistentWorld`.

## Data flow

```text
PersistentWorld::submit(command) — SQLite durable submission
   ↓
PersistentWorld::step() — stage, execute, COMMIT, then publish in-memory state
   ↓ (only after successful step returns)
ServerViewBridge::after_committed_step(world) — snapshot diff + newly committed events
   ↓
encode_frame(ViewFrame) → interprocess pipe (test) → decode_frame(bytes)
   ↓
ClientView::apply(frame) — staged entity/job updates; validate final hash
   ↓
Client-visible current world + recent notifications
```

Frames are **not** permissions to act. The client is a read-only projection; the existing ship owner validation remains in the game simulation. Network session authorization is out of scope.

## Packet framing

Line-oriented UTF-8/ASCII numeric values only; newline framing; bounded to 4 MiB and 60,000 changes per frame.

Header:

```text
SGVIEW|1|<kind>|<stream>|<sequence>|<tick>|<system>|<projection_hash>|<change_count>
```

Kinds: `1` snapshot (all visible entities and active jobs), `2` delta (changes since previous tick). Last line `END` and newline, with no trailing bytes.

Change records:

| Token | Meaning |
|---|---|
| `E` | Entity upsert: ID, system, type, owner, position x/y, velocity x/y, ore, cargo, capacity |
| `D` | Entity remove: ID |
| `J` | Active mining job upsert: actor, target, amount/tick, start command ID |
| `X` | Active mining job removal: actor ID |
| `C` | Command result: tick, command ID, outcome, transferred amount |
| `A` | Activity event: tick, kind, start command ID, actor, target, outcome, transferred amount |

`E` carries all current fields for idempotent entity display state. Client-side incremental messages represent resource and position changes without changing game rules. On full asteroid depletion, an entity `D` plus an `AsteroidDespawned` activity is delivered, which the renderer can map to a visual disappearance. Active mining is visible through `J`/`X`, and a `MiningStopped` activity carries the precise reason (`CargoFull`, `NoResources`, etc.).

**Important:** These tokens and message names are **our own design**, not a claim that Crucible uses them.

## Sequence and reset

- Epoch (`stream_id`) changes when a host/adapter restarts; the host must supply a unique nonzero value in actual deployments.
- The first full snapshot is sequence 0 in a new bridge. Each committed `step()` increases the sequence by 1.
- All deltas need matching epoch, current system, sequence exactly +1 and tick exactly +1.
- Missing/out-of-order deltas request a fresh snapshot; stale snapshots from the same stream cannot rewind local state.
- An old-epoch delta cannot overwrite the state after a new-epoch snapshot.
- Client state is staged, hash-validated, then published. The hash catches ordinary transport/coding mistakes but is *not* cryptographic integrity protection.
- The adapter observes historical event-vector offsets when attached; restarting it does not reemit old notifications.

## Validation requirements before network use

A production online transport must add authenticated sessions, interest management and visibility policy, encryption and authenticated messages, bandwidth bounds, per-client acknowledgement/retransmission or snap resync, rate limits, and fuzzing of packet parsing. Runtime tick scheduler and client smoothing/interpolation should be designed with actual physics rules (the current game uses inertia), not by assuming EVEmu runs every spatial subsystem at 1 Hz.
