# B4.3 Live Player Control and Presentation Smoothing

**Project:** Original Single Galaxy / one test system. Independent of EVEmu and historical EVE assets.

## Runtime

- Native `galaxy_interactive_host` owns `PersistentWorld`, full simulation state, one fixed-step clock, and the `ServerViewBridge`.
- Native host publishes only after successful `PersistentWorld::step` / SQLite COMMIT.
- The local Python `interactive_relay.py` maintains **one host process per relay launch**, not a new world per browser connection; a bounded frame history is replayed to reconnecting spectators before subsequent live deltas.
- A browser obtains a per-relay random anti-CSRF token via same-origin `/session`; POST `/command` checks the exact loopback Host, Origin, JSON content type, token, length, schema, and field types. A 202 response means **queued**, not executed or committed.
- Commands carry only an allowlisted action (thrust axes -1..1; start mining 1..10 ore/tick; stop mining). The host assigns issuer=7, the owned ship ID, target, unique command ID, and next tick. No authority-bearing values can be chosen by the browser.
- C++'s original command validation and durable receipt mechanisms remain authoritative. An accepted command's result appears in subsequent SGVIEW `C|` command-result records. The local broker token is **not player authentication** or authorization against a hostile process with local machine access.
- `SGVIEW|1|` still includes stream epochs, contiguous seq/tick, and consistency hash; `GalaxyContract.applyNext` validates a single incoming frame before the viewer publishes it.
- Browser rendering alone interpolates previous/current authoritative entity positions over the configured step period. Entity add/remove and mining/cargo values come immediately from committed state. Interpolation does not change gameplay or resource totals.

## Known limitations

- World is ephemeral per relay *launch*; SQLite lives in a temporary folder and is removed on shutdown. This is a developer demo, not a durable user save slot.
- Host runs a bounded number of fixed steps (default 180) and exits. A viewer may reconnect while it runs or after completion; reconnect replays the bounded same-world frame history, not an instant current-tick snapshot. No unbounded replay/history compaction yet.
- Only a single **local player** identity with one ship, a rock and a station is available. Any same-origin browser page can act as that player. No multiplayer, hostile-local-process security, user accounts or per-client ownership yet.
- Inertia units are millimeters/logical-tick; interactive tick interval must be calibrated before production physics.
- Browser client is pure original canvas shapes, not a 3D renderer. No independent real Windows MSVC/Edge acceptance unless explicitly tested.
- Source repo is independent of EVEmu. No proprietary artwork or DLLs are packaged.
