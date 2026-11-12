# S6-F5-C Native Integration — Original Gravity, Vertical Physics and Survival
**Date:** 2026-11-12. **Branch:** `sg/stage6f5c-native-gravity-survival-20261112` (stacked on `sg/stage6f5c-godot-host-vertical-20261112`). **Status: source staged; native compile, CTest and physical Windows acceptance not executed.**

## Authoritative physical catalogue

- `PhysicalCelestialBody.physical_mass_1e18_kg` (exatons of kilograms) is v134 canonical-source data, generated from original immutable astronomical body IDs/radius and a pinned deterministic, synthetic bulk-density profile. The generated mass is **game-world synthetic data**, not measured astrophysical mass. Gravity uses integer `66743 * mass_exatons / radius_km^2` mm/s² (fixed-point SI G), with checked bounds. Star/debris bodies have no native walking gravity.
- Only an original site-verified authorized v134 operation may promote a v70–v133 celestial catalogue after independently matching its complete original body identity, kind, orbit, position and radius to the deterministic generator. On recovery the new mass is checked against the original generator. Old saves are not silently upgraded at file open. No mass is extracted from a Godot mesh, guessed player location or invented resource.

## One real player and original SQLite journal

- `NativePlanetarySurvivalState` now belongs to the canonical `PlayerPerson`: radial position, vertical velocity, grounded state, last authoritative vertical tick, personal oxygen in whole kg, captured CO2 in whole kg, oxygen-use tick remainder, native hazard exposure and impact injury (0–100). Persisted in `galaxy-kernel-v134`, with strict field count and bounds, source cross-validation and old-save decoding.
- `Simulation::step` advances radial gravity at a single canonical game-step quantum (100 ms), taking the **original** planet-relative position, immutable source survey triangles and persisted physical regional hazards. It never reads Godot collision impulses. The original pose Y is updated from the native radial solver without client height; airborne walking cannot snap through source terrain.
- New `PersistentWorld::planetary_surface_vertical_action` commits one actual original world STEP and an explicitly approved JUMP or WAIT in the *same* SQLite transaction, with one-use original movement receipts, fully recovered canonical v134 state and conservation checks. A failed write retains the old world state.
- Original site oxygen can be allocated only at the real hatch via `PersistentWorld::planetary_surface_oxygen_action`. One source kg is debited from `PlanetarySiteFacility.oxygen_kg`, held in the canonical player, and converted to one original person-carried CO2 kg after 600 100-ms committed gameplay ticks. Unspent O2/CO2 must return to the exact original site before reboarding; no instant replenishment or fabricated suit. This is a bounded personal **breathing-stock allocation**, **not certification of a manufactured pressure suit** or breathable-atmosphere proof.
- A verified original Heat/Cold/Flood hazard increments thermal exposure and bounded injury. Independently observed hard landings add bounded impact injury. Missing breathing reserve in an atmosphere with **unknown breathability** records an explicit uncertain-air exposure, but must not manufacture hypoxia injury. Player injury persists after return to ship. No automated medical healing is inferred.

## Native installed game ingress

`galaxy_desktop_player_host --planet` is still a MODE of the existing original executable: supervised, writer-excluded, and admission-gated on the previously landed original player/ship/site/shuttle. New command forms:

```text
PMOVE|SESSION|REQUEST|DX_MM|DZ_MM
PSTEP|SESSION|REQUEST|JUMP
PSTEP|SESSION|REQUEST|WAIT
POXY|SESSION|REQUEST|CHARGE
POXY|SESSION|REQUEST|RETURN
```

Only acknowledged `PFRAME` shows original body gravity, committed height, velocity, grounded status, oxygen/CO2, thermal exposure, uncertain-air exposure and injury. The original player controller proposes Space for jump, idle WAIT ticks, F8 for site oxygen charge, F10 for return, and WASD for motion. It does not invoke local Godot physics as world authority.

## Safety and open acceptance

- The original separate `godot/stage6` tile visualization has not been embedded into the installed game's character scene. A player may get authoritative original pose receipts while no native terrain mesh is displayed: **do not call this playable continuous 3D planetary terrain**.
- There is no verified source composition/pressure/breathability, manufactured pressure suit or thermal insulation ledger, life-support pipeline, injury treatment or rescue integration. Unknown air alone is *not* proof of lethal hypoxia. O2 in kg is consumed only at committed tick boundaries; this is coarse conservation, not detailed gas physiology.
- Existing `galaxy_stage6_native_surface_movement` now includes positive v134 original-player jump/land, replay denial, mass-tamper refusal, oxygen debit/return when stocked, SQLite recovery-copy, and injected failed commit; `galaxy_stage6f5c_body_gravity` independently checks fixed-point mass/gravity bounds. These are authored and registered, **not run**.
- Remaining closeout: exact-head native Windows/Linux compile and CTest; positive original host/Godot F9/Space/F8/F10 end-to-end session on a real landed original save; transient/failure/race/cross-process recovery; original medical and suit inventory and environmental composition; F5-D named NPC pathing; F5-E actual installed tile/character integration; F5-F and S6-G acceptance. Do not merge or release on source-only evidence.
