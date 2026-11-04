# PLAY-05 — Mining Expedition Feedback and Completion

**2026-11-04 · Single Galaxy · gameplay-facing branch `integration/play05-mining-expedition-feedback-20261104`**

## Player embodiment is undecided

The current client boots into **direct control of one existing colony ship**, whose finite crew is simulated. This is the implemented gameplay perspective, **not** a finalized decision that the player is a disembodied ship, nor a promise that the game begins with a controllable body who walks aboard and then assumes control. The project has not chosen between a persistent player avatar, a cinematic ship-entry sequence, or direct ship start. Avoid coding an avatar/boarding mechanic until separately decided. The skippable tutorial remains independent of this question.

## PLAY-05 implementation

Add one clear mining-expedition HUD to the existing live colony view, with nearest/selected target readout, live mineable ore and distance, native refusal explanation, currently carried bulk ore and observed deposits at the player's real constructed home outpost.

The new `viewer/expedition-feedback.js` deliberately has **no world mutation authority**. It accepts validated ship and hangar projections, refuses to infer success from a queued HTTP command, starts with no prior history on reconnect, and tracks a single session's bulk ore expedition only when:

1. Native world projections show the ship's bulk cargo increase while not docked.
2. A verified player-built home outpost is visible.
3. Later authoritative frames show the ship docked at that same home.
4. Native hangar ore increases while docked, bounded by bulk ore mined and not already recorded as delivered.

Do not equate browser session milestones with persistent quest records. Native SQLite ore ownership and conservation remain the source of truth, including across reconnects. Historical expeditions cannot be reconstructed from the current snapshot alone. Multiple different actions can produce an `APPLIED` result; the HUD labels that generically and only as an actual native command outcome, without assuming which queued browser click it matches.

Explain `OUT_OF_RANGE`, `CARGO_FULL`, `SHIP_MOVING`, `NOT_DOCKED`, `NO_RESOURCES`, `UNAUTHORIZED`, etc. from native `RESULT` telemetry. A script referenced in HTML must also be served by the local relay allowlist; this phase fixes PLAY-04's previously unserved `cargo-return.js` and adds `expedition-feedback.js`.

### Limitations and acceptance

- Only **bulk ore** uses this proof of home delivery. Geological named mineral stock, fabricated modules, finished goods, crew and manufactured item custody have separate native systems.
- Status and target range are **advisory**. The authoritative native host still validates range, identity, ownership, capacity, docking and physical movement.
- No automatic autopilot, instant-braking, player body/boarding or NPC transportation gameplay is introduced.
- UI verification includes Node pure-function tests, browser/relay static-path contract checks and JS/Python syntax; native PLAY-04 mining/docking/save test should be executed unchanged. Browser interactive click-through on the real host, Windows inputs and long expedition timing remain acceptance gates.
- The already optional first-outpost tutorial stays optional.

## PLAY-06 handoff

Prioritize a comfortable ship-level HUD, selective camera and zoom behavior, movement input acceptance, and error-free local play sessions before any extension into walking avatars or NPC migration.
