# B0 — Original Single-Galaxy Architecture Contract
**Draft date:** 2026-10-30  
**Classification:** Original design; partly implemented in B1; not a final gameplay specification.

## World and scope
One original fictional galaxy is the top-level world container. It may eventually contain multiple star systems and coarse inactive sectors. **B1 activates only one small star system.** No EVE universe, compatibility schema, factions, map, lore, assets or historical client dependency. The number of systems, final movement scale, gameplay economics and eventual graphics technology are undecided.

## Authority boundaries

```text
[Future local UI/input]      [Future optional network transport]
            \                      /
             \                    /
               [Command adapter]
                      |
         [Authoritative simulation core]
           |          |           |
     [Movement] [Resources] [Future NPC/economy]
                      |
          [Future transaction coordinator]
                      |
              [Future SQLite store]
```

- Clients/adapters submit **intents**, never direct mutations of authoritative world data.
- The current kernel only accepts typed commands. The host may explicitly instantiate entities for test scenarios. A multiplayer host must restrict host-only creation and derive issuer from an authenticated session (never trust arbitrary client-provided issuer IDs).
- Each accepted command carries globally unique ID, target logical tick, sequence, issuer and actor. Duplicates/past-tick commands are rejected; successful submission does not imply execution succeeds.
- On each tick: sort scheduled commands by `(sequence, command_id)`; execute with preconditions; append result event; move ships in stable entity-ID order.
- Current integer positions and velocities are measured in mm and mm per tick; no wall clock inside the core. A nominal 100 ms real-time adapter may be implemented later, but **no scheduling accuracy has been tested**.
- Explicit initial galaxy/system frame: `system_id=1`. The present bounded 2D arena is only a smoke-test coordinate frame. Later: local sectors, high precision frames, 3D and stellar distances. Do not infer astrophysical coverage from current limits.

## Invariants implemented and tested in B1

1. **Determinism:** given same build, seed, initialization and command stream, the same state and event sequence result. Cross-platform replay still requires Windows confirmation.
2. **Command ordering:** `(execute_tick, sequence, ID)` gives deterministic replay independent of order of command submission within a tick.
3. **Authority:** only matching `issuer`/ship `owner_id` commands alter ship state. This is an in-process authorization guard, not a network identity system.
4. **Finite extraction:** mining transfer `q=min(request, asteroid_ore, remaining_cargo)` if valid; `asteroid_ore + ship_cargo` remains constant for isolated mining scenarios.
5. **Atomic in-process transfer:** a mining command updates ore and cargo in one non-interleaved transition after validation. Exceptions, power loss and database failures are NOT yet covered.
6. **Speed and position bounds:** per-axis velocity capped, movement clamped to the demo arena, outward velocity at an edge stopped.
7. **No hidden spawning:** B1 mining moves already-existing ore; it does not create ore through extraction.
8. **Explicit event result:** each processed command records outcome and quantity transferred. Event histories currently grow without pruning.

## Deferred interfaces

| Subsystem | Boundary for implementation | Gate |
|---|---|---|
| Durable storage | Idempotent command journal, snapshots, SQLite transactions, schema versions, restore/checksum | B2 |
| Economy | Stock-backed orders, wallets, tax, production/consumption ledgers, reconciliation | B3 |
| NPCs | Deterministic policy/state machines, scheduling, remote-sector coarsening | B3 |
| Gameplay | Launch/dock, ship travel, salvage/extraction, return/sell, restart | B4 |
| Profiling | Repeatable multi-scenario CPU/RAM/transaction/tick/recovery measurements | B5 |
| Graphics/network | Standalone original client interface and optional authenticated host adapter | B6 |

## EVEmu influence without copying

The educational reference demonstrates the importance of separation between client presentation and server-owned state, independent world/inventory/market services, and fault-aware persistence. P4 specifically demonstrated functioning database transactions *without* directly validating complete MarketMgr order paths. The original backend will therefore make transactional state coordination testable without linking a monolithic game executable. No EVEmu implementation, protocol, asset or database identifier is imported into this codebase.

## Explicit non-goals

No online service; no EVE client or protocol compatibility; no megaserver; no fully populated galaxy; no claims of realistic celestial mechanics; no production-safe persistence, multiplayer security, economy or graphics in B1.
