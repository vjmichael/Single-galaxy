# Stage 6 original planetary mesh / Godot 4 preview

This isolated Godot project is a **render-only proof-of-integration**, not the canonical game. A C++ source-world adapter, `galaxy_stage6_godot_export`, opens an **existing original SQLite world**, verifies the ship/owner/body and generates up to four original watershed tiles in `SG-STAGE6-GODOT-01` JSON. No default planet is fabricated.

Example developer workflow (paths and IDs must come from an existing original game save):

```sh
galaxy_stage6_godot_export /path/to/original.sqlite 7 ORIGINAL_SHIP_ID ORIGINAL_BODY_ID /tmp/stage6-planet.json
godot --path godot/stage6 -- --packet /tmp/stage6-planet.json
```

The Godot scene constructs an ArrayMesh and matching ConcavePolygonShape3D faces from each original vertex/index batch, places a camera and light, and never changes the simulation or grants player landing. Numeric geometry is visual unit-sphere data; identifiers, hashes, generations and the body radius are separate 64-bit-precise string values. No generated mesh may serve as a physical authoritative ship or player transform.

The viewer records 300 actual client frame deltas after loading the original mesh and writes p50/p95/p99/max milliseconds to `user://stage6_frame_pacing.json`. These are measured render-client frame intervals, **not GPU execution timings** and not physical Radeon AI Pro R9700 evidence unless run on that machine. Preserve the exact native world generation and hash alongside any local test record.

**Not yet accepted:** native-Godot headless parsing/run, measured actual frame time or Radeon AI Pro R9700 performance, 3D player-character landing and re-embarkation, live tile paging/eviction, sphere cap seam, Godot↔original executable integration. This source-only adapter is not proof of those items. Actual player arrival remains conditioned on a separate authoritative original `NativeLandingObservation` and live game authorization.

## STAGE6-D2 — bounded native tile paging (source implemented; runtime OPEN)

The original native exporter can select **one to four** genuine survey tiles per invocation. For example, after authorizing the existing owner/ship/body IDs above, an operator can republish the same packet file with the next two original longitude bands:

```sh
galaxy_stage6_godot_export /path/to/original.sqlite 7 ORIGINAL_SHIP_ID ORIGINAL_BODY_ID /tmp/stage6-planet.json 0,4,1 0,5,1
```

Each address is `LAT_BAND,LON_BAND,STRIDE`, with latitude bands 0–4, longitude bands 0–11 and stride 1 or 2. The exporter validates the original owner and exact world generation/hash, refuses output paths aliasing the SQLite save or its WAL/SHM/journal, and atomically publishes the new JSON without exposing a truncated packet.

The Godot scene polls the operator-selected file every half-second. After validating every tile in the batch, it replaces the affected band/LOD, retains **at most twelve** visual mesh+collision tiles and evicts the oldest. Changed original generation/hash, planet, owner or source ship clears all older tiles. The rendered trimesh is purely **Godot preview collision**, not native person/ship landing or world physics authority. This file-based paging is an intermediate developer adapter; it is **not** a live host subscription, planet streaming worker, full polar cap or a playable landed character.

After each complete accepted tile batch, the client captures 300 frame deltas and records client p50/p95/p99/max together with resident tile count, exact original body/generation/hash, the Godot-reported graphics adapter/vendor and rendering driver. Empty adapter fields in headless runs are not physical GPU acceptance. A physical **Radeon AI Pro R9700** test needs an actual Windows Godot run, saved evidence of those fields and observed camera/collision/LOD behavior; this source change alone cannot claim it.

WO3-F historical SQLite migration remains a separately mandatory acceptance gate: no modern snapshot, simulated SQLite provenance or source-only CTest can replace the genuine v86 native archive and SHA-256 inputs.

## S6-F original NPC landing and bounded inspection (source staged; not player arrival)

The S6-F branch adds a **read-only** `--landed=LAT,LON,1` option to the original world exporter. It is accepted **only** while the existing original, owner-controlled, named-NPC-piloted shuttle has completed its physical descent to a constructed planetary spaceport and its one-use native descent receipt remains current. Without explicit tile addresses the exporter selects that original landing region's own mesh band. The original SQLite source is opened `SQLITE_OPEN_READONLY` through `PersistentWorld::recover_copy`; the restored working copy is temporary and any separate Godot polygon collision is not world authority.

```text
galaxy_stage6_godot_export EXISTING.sqlite OWNER_ID SCANNER_SHIP_ID BODY_ID stage6.json --landed=LAT,LON,1
godot --path godot/stage6 -- --packet stage6.json
```

When this packet's original receipt, pilot NPC and original surface location are all valid and the referenced landing tile is actually present, `SurfaceInspector.gd` opens a Godot **local inspection character** with WASD/arrow movement, mouse look, Esc cursor release, spherical visual gravity and native-mesh-derived local collision. Its HUD explicitly identifies it as **NOT the original player**. On return/ascent, stale or mismatched native packets do not grant continued inspection. No packet can create world resources, approve a player landing, store native footsteps or cause an original ship or NPC to teleport. Full real player transport, source-authoritative terrain-walk receipts, live installed-game integration, latitude polar caps, Windows/Linux native and physical R9700 acceptance remain OPEN. See `docs/engineering/STAGE06F_PLANET_ARRIVAL_TERRAIN_20261111.md`.
