# NET-01 — Deferred Multiplayer Transport and Host UX Decisions

**Decision date:** October 31, 2026
**Status:** Architecture recorded; **no server GUI, launcher, or production multiplayer implementation authorized in this milestone**.

## Current verified networking and launch code

- C++ `Simulation` and `PersistentWorld` remain authoritative for game rules and SQLite durability.
- The current B3 `SGVIEW|1` protocol is **versioned ASCII numeric ID-centric snapshot/delta** data. Frames carry entity IDs, positional/resource state, removals, jobs, command results and committed activity, with stream epoch, sequence/tick and projection checksum. It is not an EVEmu protocol clone.
- `live/interactive_relay.py` provides **development-only loopback HTTP** command forwarding and Server-Sent Events (SSE), relaying SGVIEW frames from C++ host stdout and commands to stdin. The browser uses `/session`, `/events`, `/command`; the relay is not a multiplayer security boundary.
- Windows .cmd scripts currently expect Python. A one-click bundled native runtime and server administrator GUI **do not exist yet**. Do not call the current setup production multiplayer.
- `docs/B3_client_contract_spec.md` documents current protocol and missing network authorization.

## Accepted future architecture

1. **One authoritative simulation implementation:** local single-player, dedicated host and embedded multiplayer use the same server rules, persistence and command validation. No divergence into separate games.
2. **ID-centric binary transport (future):** replace/extend SGVIEW ASCII over the wire with an explicitly versioned compact binary protocol (schema/field IDs, integer entity/object IDs, snapshots + changed fields/events, bounded frame size and negotiated compatible versions). Do not pick Protobuf/FlatBuffers/custom coding without later performance and compatibility tests.
3. **Client authority:** clients send intentions, never accepted effects; server validates session, entity ownership, action legality and state. Filter world data by visibility/interest. Use per-client sequence/epoch acknowledgment and snapshot recovery. Distinguish protocol consistency checks from cryptographic authentication.
4. **Real networking:** authenticated encrypted transport, session/reconnect tokens, rate limiting, fuzzing, bounded queues, reliable state/event classes and movement interpolation. Support headless dedicated servers for unattended operation.
5. **Invisible Play startup (future):** a bundled native supervisor starts a *local* server privately, loads/verifies the save, waits for explicit ready/healthy status, then launches the client. User does not need Python, Docker, terminals or manual port entry.
6. **Graphical Host Server mode (future):** the same supervised backend with an optional server management application for world selection, start/stop, backups, performance/health metrics, live logs, player/session controls and debugging of NPC societies. Host UI is independent of the server lifecycle and uses a separately authorized administration API. No omniscient player-side exposure through the normal game client.
7. **Distribution:** one straightforward single-player installer packaging game/client/server; optional standalone dedicated-server package; GUI optional for headless deployments.

## Deferral and next action

**Defer** protocol replacement, launcher UX, management screens, installer and live multiplayer until specifically scheduled. Do not route current GRSM milestones through an unproven networking rewrite. Continue **GRSM-04b.B** to establish named NPC action execution through independently verifiable *committed* world effects, with a durable source receipt and negative tests for guessed causality.

GitHub is a source/milestone record only; primary development and testing remain local.
