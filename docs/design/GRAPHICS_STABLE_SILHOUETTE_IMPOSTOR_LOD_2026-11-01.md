# Single Galaxy — Stable-silhouette LOD / impostor rendering direction

**Design decision recorded November 1, 2026. This is not yet a working renderer.**

The user strongly prefers to avoid recognizable model popping and triangle reduction when traveling toward or away from a 3D object. Preserve the apparent silhouette and use 2D image/animation stand-ins where they look convincing.

## Rendering tiers
1. **Nearby:** original 3D model, animation, interaction, physically meaningful lighting/collision.
2. **Intermediate:** preserve silhouette-critical geometry, hide or simplify inconspicuous attachments, materials, and draw-call-intensive details rather than aggressively changing the object's outline.
3. **Far:** multiview baked impostor atlases (potentially octahedral), with albedo, depth, normals and material metadata so shadows/lighting can be approximated. Engine exhaust, warning lights, rotors or similarly small motions can be overlaid separately.
4. **Tiny on screen:** point/light representation where physically appropriate, otherwise graphics-only culling. A scanner contact and authoritative object identity continue to exist.

## Preventing visible transitions
- Decide graphics representation by projected screen-space error, angular movement, apparent size and camera speed—not physical distance alone.
- Use hysteresis plus temporal dither or depth-aware fades, preloading both representations across a transition band.
- Test camera orbit, bank/roll, zoom, high-speed approach, warp arrival, shadows and occlusion. A rotating, articulated ship may require full/silhouette-preserving geometry longer than a station or asteroid.
- Multiview impostors take bake effort and texture memory, especially with player-built, reconfigurable industrial installations. Fall back to real geometry when an impostor breaks parallax, depth, reflections or stereo viewing.
- Per-camera graphics LOD must never alter server truth. In multiplayer, one observer may see a billboard while another sees a detailed ship. Same physical identity, cargo, and AI state.

## Boundary with regional simulation
The earlier `RegionalInterestManager` suggests which regions need detailed simulation; the new `RegionalModeAuthority` persists who is allowed to change game state. Graphics LOD does **not** authorize inventory consumption, ownership handoff, object creation/destruction or shipment arrival.

## Future acceptance
Implement actual bake/runtime impostor assets only after identifying the rendering engine and target content pipeline. Capture visual transitions (not only static images) in multiple lighting and flight conditions. Profile texture memory, GPU time, streaming stutter and object selection accuracy on real Windows hardware.

This is a target for graphics work, not code already shipped.
