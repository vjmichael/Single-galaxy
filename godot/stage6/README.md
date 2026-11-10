# Stage 6 original planetary mesh / Godot 4 preview

This isolated Godot project is a **render-only proof-of-integration**, not the canonical game. A C++ source-world adapter, `galaxy_stage6_godot_export`, opens an **existing original SQLite world**, verifies the ship/owner/body and generates up to four original watershed tiles in `SG-STAGE6-GODOT-01` JSON. No default planet is fabricated.

Example developer workflow (paths and IDs must come from an existing original game save):

```sh
galaxy_stage6_godot_export /path/to/original.sqlite 7 ORIGINAL_SHIP_ID ORIGINAL_BODY_ID /tmp/stage6-planet.json
godot --path godot/stage6 -- --packet /tmp/stage6-planet.json
```

The Godot scene constructs an ArrayMesh and matching ConcavePolygonShape3D faces from each original vertex/index batch, places a camera and light, and never changes the simulation or grants player landing. Numeric geometry is visual unit-sphere data; identifiers, hashes, generations and the body radius are separate 64-bit-precise string values. No generated mesh may serve as a physical authoritative ship or player transform.

**Not yet accepted:** native-Godot headless parsing/run, measured actual frame time or Radeon AI Pro R9700 performance, 3D player-character landing and re-embarkation, live tile paging/eviction, sphere cap seam, Godot↔original executable integration. This source-only adapter is not proof of those items. Actual player arrival remains conditioned on a separate authoritative original `NativeLandingObservation` and live game authorization.
