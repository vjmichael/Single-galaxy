# PLAY-13C.2 — Celestial local-frame entry: implementation checkpoint

## Scope committed

- `CosmicLocalFrame.hpp`: checked, reversible local/cosmic translation with the authoritative existing ±1,000 km simulation bound. Unsupported 3-D to 2-D projection is rejected.
- `CelestialFrameTransition.hpp`: read-only host transition planner, keyed by owner, ship, system, celestial ID and a **caller-verified completed transit ID**. The ID parameter is a contract, not proof or authentication by itself.
- No arbitrary destination coordinate may be submitted by the browser. Target identity must exist in the actual v70 body registry, and frame origin is computed from saved geometry at a 100 km standoff beyond nominal body radius.
- Gate checks: real ship ownership; valid existing same-system body; undocked and not jumping/docking; no active mining; no deployed carried probes. Arrival would have zero local position/velocity.
- Tests: self-contained coordinate bridge; native Simulation-linked authorization, immutable-state and provenance preflight; Linux/Windows CMake workflows.

## Safety boundary — NOT YET LIVE

This is a **nonmutating arrival plan**. It is not an implemented travel command and must not be advertised as a ship reaching another planet. It does NOT use the browser, execute a warp, consume fuel or apply elapsed travel time, commit an arrival, move personnel, persist a ship frame or publish a host receipt. A bare nonzero transit identifier would be unsafe if used by a user-facing endpoint.

## Implementation gates before PLAY-13C.2 is complete

1. Add a native host-controlled `InterplanetaryTransit` lifecycle and idempotent, non-forgeable completion ledger. Caller owns custody of a validated transit receipt; a UI string or generated number is NOT trusted evidence.
2. Represent each ship's active local frame in authoritative `Simulation` state with backward-compatible versioned persistence, canonical restore validation and recovery-copy invariants.
3. Perform arrival as **one journaled transaction** with receipt consumption, source-frame exit, destination-frame entry, ship position/velocity update, observation and state publication. Verify failure injection and power-loss restart before claiming success.
4. Decide policy for deployed surveys, towed cargo, in-flight projectiles, active rescue/evacuation and occupants, and verify actor gating on every simulation action that assumes a shared local origin. The existing physics partitions only by system, so merely attaching per-ship frame metadata would otherwise permit impossible cross-frame mining, docking and combat.
5. Provide server-confirmed departure/arrival and frame-specific projection in authenticated relay and map. Do not expose high-resolution undiscovered destination coordinates.
6. Pass real Windows/Linux C++ CI plus integrated world restart, duplicate replay, wrong-owner, wrong-system, torn-write and resource/person conservation tests.

## Important architectural finding

`Simulation::kWorldBoundMm` is **1,000,000,000 millimeters (±1,000 km)**. The bridge's previous `1,000,000,000,000` millimeter bound was too large by a factor of 1,000 and was corrected. Existing `Entity.position` remains 2-D; changing the simulation bound or treating every entity in one star system as sharing the same local origin would be unsafe.

PR #47 remains stacked on PLAY-13B #46, draft, until all gates are actually verified.

## PLAY-13C.2B — v71 durable ship-frame custody checkpoint

Implemented on the stacked draft branch:
- `ShipFrameCustody` in authoritative `Simulation`: `system`, `anchor_body`, and 3-D cosmic frame origin, keyed by the existing ship entity ID. This does not clone or move a ship.
- `Simulation::ensure_ship_frame_registry()` upgrades only preexisting ships to unanchored origin-zero frames. New ships added to a v71 world receive an unanchored frame automatically. Repeated migration is idempotent.
- Version 71 snapshot headers and canonical `shipframe|ship|system|anchor|x|y|z` rows. Strict restore rejects missing/duplicate/orphaned frame custody, mismatched ship systems, forged unanchored origins, nonexistent body anchors, and anchors with modified origin relative to physical body radius plus 100 km stand-off. Earlier v70 snapshots remain readable and only migrate through an explicit committed world operation.
- `PersistentWorld::ensure_ship_frame_registry()` commits the full v71 world through the existing atomic SQLite snapshot journal and publishes the new in-memory state only after commit.
- The actual colony host explicitly upgrades saved worlds after physical celestial initialization.
- `PLAY13C_DurableShipFramesTests.cpp` covers two ships/owners, idempotence, owner/crew/mass continuity, exact snapshot reload, missing/duplicate/tampered records, SQLite reopen, and recover-copy. The Windows/Linux native CI matrix builds this test and the live host.

**Still not implemented:** a host-authenticated departure/arrival command, completed-transit receipt verification, consumed receipt journal, frame-local entity interaction partitioning, and a UI. v71 metadata is the durable representation needed for these; it is not itself an interplanetary movement feature.

**Gate status:** implementation committed, **CI not yet confirmed**. Do not mark 13C.2B verified until the actual current-head native builds and tests pass. Next technical milestone is PLAY-13C.2C, authoritative transit lifecycle/receipt with cross-frame interaction guards before enabling any nonzero celestial anchor.
