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
