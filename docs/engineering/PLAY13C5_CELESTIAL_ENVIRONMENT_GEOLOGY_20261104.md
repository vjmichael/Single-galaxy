# PLAY-13C.5 — Durable Celestial Environment and Mineral Differentiation (v74)

## Implementation checkpoint

PLAY-13C.5 builds on **conserved v73 regions**, without inventing additional ore, fabricated terrain meshes, atmosphere damage, NPCs, stations, or free replacement resources.

### Deterministic body profiles

Every generated physical region has a native `LocalCelestialEnvironment` record derived solely from its **persistent astronomical body** and the fixed astronomy hash. The record contains:

- `zone`: 1 stellar radiation, 2 debris region, 3 rocky planetary, 4 outer icy planetary, 5 lunar
- `hazard`: bounded 0–3 **observational severity only**. It is not yet damage, collision, gravity or biological life-support effect.
- `first` and `second`: two distinct `AsteroidClass` values (carbonaceous, silicate, metallic) for the two persistent deposits created by PLAY-13C.4.

The body class and orbital radius guide the zone; the stable mix of `body.id` deterministically diversifies mineral classes. All environment outputs are game hypotheses, not astrophysical measurements.

### Resource and mining custody

The actual v73 physical rock entities remain unmodified in their **tonnage and unique identity**. A new `local_deposit_taxonomy(rock_id)` resolves a currently-live site's mineral type from the immutable `localregion|body|siteA|siteB` manifest and its v74 environment. It returns no taxonomy for depleted, missing, forged, or off-region IDs.

Both one-shot `Mine` and `StartMining`/autonomous mining now route locally classified ore through the existing `transfer_geological_minerals` path for actual colony ships: mined tonne mass is divided into its taxonomy's fixed permille mineral fractions and stored as **kilograms in real ship mineral inventory**, never created in parallel as ordinary bulk cargo. This preserves world `total_ore`/physical mass and all normal cargo capacity, transfer, exhaustion and SQLite conservation checks. Other vessels retain their normal raw-cargo rules.

This intentionally does **not** reuse the existing `AsteroidGeology` spherical radius model for 6- and 8-tonne deposits: that older model enforces a 10-m minimum radius and large spherical mass, so falsifying a radius to make a tiny reserve look like a massive rock would corrupt its independent physics verification. Local-region mineral taxonomy is separate, small-deposit metadata backed by the immutable region manifest.

### Save format and migration

- `galaxy-kernel-v74` adds `localenvironment|body|zone|hazard|first_taxonomy|second_taxonomy` canonical rows.
- Strict restore rejects invalid codes, duplicate records, missing or extra environment rows, absent region custody, or a mismatch with the original physical body's independently regenerated profile.
- `Simulation::ensure_local_environment_profiles` and `PersistentWorld::ensure_local_environment_profiles` safely upgrade an existing v73 generated region **without recreating its ores, entities, depleted sites, or altering its cargo**.
- First-time arrivals still generate region and profile within the **same atomic SQLite world transaction**. A host restart performs any remaining upgrade through the existing journal before publishing new output.
- The live host's local scanner emits `LOCAL_ENVIRONMENT|body|zone|hazard` and `LOCAL_DEPOSIT|site|taxonomy`. The browser contract validates these messages and shows environmental severity as a descriptive indicator plus actual per-site mineral class.

### Acceptance

`tests/PLAY13C_LocalEnvironmentTests.cpp` exercises first star environment, different planetary zone, two distinct site mineral classes, exact real 1-tonne mining -> 1,000 kg fractional mineral inventory with no duplicate raw cargo, unaltered mass and crew, exact SQLite reopen, missing/modified v74 profile rejection, and v73-to-v74 canonical migration with material conservation. Existing PLAY-13C.4 and real host smoke updated to expect v74, and Windows/Linux CMake native matrix includes new C.5 target.

**Gate not yet verified:** GitHub Actions and actual native Windows/Linux acceptance must succeed on the exact branch HEAD before PR #47 may be treated as passing. Local container cannot currently reach GitHub to independently clone and compile; no pass has been inferred.

### What C.5 does NOT claim

No procedural planet surface, volumetric weather, ecosystem, thermal damage, atmosphere simulation, gravitational trajectories, surface landing, voxel geology, or actual radiometric mining instruments. Existing space coordinates are still 2D within each local frame and most non-mining anchored actions remain restricted. C.5 completes a deterministic, conserved, player-visible **environment and mineral provenance foundation**, not a fully simulated planetary biosphere.

### Follow-up engineering gates

1. Real cross-platform native C++ compilation and integrated regression closeout; fix C.1–C.5 failures before promoting the draft PR.
2. Replace bootstrap same-system donor transfers with explicit at-genesis, finite per-body geological reserves and policy-reviewed migration that never relocates live player-claimed resources.
3. Physically plausible planet-local surface geometry, true body-relative navigation, scan ranges, unique geology, physical safety/atmosphere mechanics, landing and survey missions.
4. Per-frame authenticated receipts and owner-specific event projections, and independent active-ship observer selection.
