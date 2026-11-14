# PLAY-12 — Real Deployable Survey Probes
**2026-11-04 · branch `integration/play12-physical-survey-probes-20261104`**

## Scope and dependency
Extend PLAY-11's four-slot-class physical ship fitting architecture and PLAY-10's persisted discovery ledger. The player-facing starting colony ship has two finite hardpoints: a local exploration scanner (module 1) and a survey-probe launcher (module 2). A **single real probe** begins aboard the ship; this is finite initial equipment, not a replenishing counter. Ships restored from old saves do not automatically receive starter equipment.

## Native gameplay loop
1. Ship in open space with a physical fitted launcher and onboard probe stock. **Launch probe** queues a native authorized `DeploySurveyProbe`. The probe becomes its own durable world record at the ship's current coordinates and system. Ship inventory decreases exactly one unit; total material remains conserved.
2. Fly away. The deployed probe stays at the launch site. **Scan from probe** executes `ScanSurveyProbe` at that stored probe position, not at the current ship position. It can discover currently existing same-system asteroids within 50,000 mm of the probe, adding new first-observation records to the shared owner-scoped discovery ledger. Repeated scanning does not duplicate or overwrite discoveries.
3. Return within 2,500 mm and **Recover probe**. Native recovery requires matching ship, owner and world system, a fitted launcher, valid ship state, and free onboard probe stock capacity. The same probe record is removed atomically while onboard stock increases by one. The recovered device may be redeployed elsewhere.
4. Restart/recovery-copy restores deployed probe ID, owner, original carrier, world position and launch tick. A completed recovery has no remaining deployed device and cannot be repeated for free additional stock.

## Conservation and security
Probe launcher and probe are represented by starter module-equivalent physical material (2 t each in this first-pass model). Both onboard and deployed probe mass are counted in `total_ore()`. Deployed probes must reference a real carrier with matching ownership/system and recorded accepted launch command ID; non-player clients cannot create probe records. Probe stock remains on shipwrecks for future physical salvage handling.

The existing owner-authenticated native command relay validates deployment, scan, and recovery. Browser telemetry is strict and read-only. No client-side coordinates or records become world authority.

## Current limitations
- Probe remains stationary at launch location: **autonomous flight, propulsion, drill arms, geological sampling, planetary/moon surface operations, power drain, environmental interference and destruction/loss mechanics are deferred**.
- No long-range remote relay/jamming/communications-distance restrictions yet. Probe scan only accepts same-system command control. These require a dedicated actual communications module and signal model, not an imaginary always-on cross-galaxy connection.
- Device manufacturing recipes, separate module items, hot-swappable hardpoint launchers and specialized drones must follow; the initial probe launcher and probe are pre-fitted new-genesis assets only, and cannot currently be manufactured in the dedicated equipment catalogue. No infinitely replenished devices.
- No Fold Gate or hyperlane transport implementation is claimed here. **Fold Gates serve point-to-point travel**, while one-way small/medium/large hyperlane highways support transport **and intermediate-space exploration**, including deliberate exits and separately constructed re-entry access; see PLAY-11 architecture.
- Actual native and UI test verification are required; code committed does not imply CI success.

## Acceptance
- Native CTest `galaxy_play12_probes`: one probe stock, launch and recovery conservation, insufficient resources, owner rejection, repeated scan idempotency of discovery, post-recovery invalid target, SQLite restart and recovered world copy.
- Browser parser and relay contract test `tests/PLAY12_ProbeWiringTests.js`.
- Focused GitHub workflow `.github/workflows/play12.yml`, including PLAY-10/11 regression.
