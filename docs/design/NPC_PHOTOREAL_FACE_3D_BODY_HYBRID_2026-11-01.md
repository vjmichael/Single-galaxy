# Single Galaxy — Photorealistic face / 3D body character rendering (v0.1)

**Date:** 2026-11-01
**Status:** Approved visual direction; **prototype needed**, not an implemented renderer.
**Scope:** All visible NPCs, not the macro-population simulation.

## Player-facing goal

Characters retain fully 3D bodies, clothing, hands, hair silhouettes and animation, while **photorealistic faces receive special attention through a view-dependent 2D/multiview facial-material technique**. Believability and recognizable identity take priority over maximum polygon count. Avoid uncanny, generic or face-swapping NPCs and obvious visual transitions.

This is **not** a mandate to display a single frontal photo on a flat rectangle. A flat face billboard alone breaks profile, parallax, occlusion, lighting and expressions.

## Proposed hybrid rendering architecture

1. **3D foundation:** lightweight skinned 3D cranium, jaw, ears, neck, body, eyes, teeth and hair geometry. Rig/face controls move with the 3D body and head. Preserve believable proportions and the head silhouette from every angle.
2. **View-conditioned face layer:** a consistent multiview photographic/albedo texture set projected to the face surface or shallow 2.5D face shell, potentially with UV-space neural or image-derived detail. The surface must track head turns and stay locked to the underlying person's identity.
3. **Lighting:** derive separate albedo, normal, roughness and skin/subsurface data. Remove photographic shadows from albedo so the face responds to actual station lights, day/night and atmosphere. Preserve correct depth for eyes/nose, shadows and occlusion.
4. **Expression support:** ensure compatible expressions and phonemes across adjacent viewing angles. Start with neutral, blink, smile, frown and speaking/jaw shapes, with deterministic face/rig synchronization. No view pop or temporal flicker.
5. **Angle coverage:** sample yaw, pitch, lighting extremes and close camera distances. Blend neighbor views only when facial landmarks and UV mapping align. At strong side/back views, the underlying 3D geometry and materials must remain convincing.
6. **Persistence:** an NPC's appearance seed, 3D morph recipe, material IDs, texture atlas versions and outfit variants are durable and reproducible. Reloading / changing simulation LOD cannot make a resident's face change.
7. **Performance:** only activate high-resolution facial layers for nearby NPCs, without replacing the **body** with a 2D impostor. Use smaller textures/limited facial animation for middle-distance crowds; switch whole-body rendering on/off independently based on visibility and world streaming, never as a player-visible identity substitution.
8. **Ethics/content:** avoid generating unconsented real-person likenesses or implying the NPC is an actual photographed person. Use fictional identities and license-cleared/generated textures.

## First actual prototype

Implement a three-NPC face-only rendering experiment:
- one younger adult, one older adult, one distinct occupational specialist, each with substantially different facial geometry and apparel;
- the same visible 3D body/head geometry and motion when comparing traditional 3D PBR skin textures vs view-conditioned face textures;
- a fixed scene with diffuse daylight, warm interior lighting and a strong rim light;
- camera orbit, player walk-up, conversations, blinking, phonemes, head rotation and occlusion by hands/hair/helmet;
- record before/after images, frame-time distribution, draw calls, VRAM, atlas memory, artifact/flicker frequency, and subjective recognition after reload.

**Acceptance criteria:** (a) identical recognizable identity through orbit and restart; (b) no obvious planar face or sudden facial restructuring during turns; (c) consistent physical lighting; (d) expression and speech remain believable; (e) measured memory/performance budget before expanding beyond a handful of characters. If view-conditioned faces look worse or cost too much, retain a high-quality photoreal 3D PBR face instead.

## Not yet implemented

No game-engine renderer, facial rig, consistent multiview image bake, runtime expression atlas or NPC face persistence integration has been delivered by this specification. This is an art/engineering design decision only.

## Relationship to existing art direction

- Realistic, AI-assisted PBR environments and human materials.
- 1960s–1980s practical, retro-industrial space architecture and clothing, without forcing human anatomy to be retro-stylized.
- No obvious shape-morphing LOD transitions for major asteroids, structures, or human faces.
- Warm/cold **simulation** handoffs remain separate from this visual fidelity scheme.
