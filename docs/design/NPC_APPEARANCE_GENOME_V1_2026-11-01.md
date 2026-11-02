# Single Galaxy — Compact NPC Facial Inheritance v1

**Status:** Working local C++17 data/procedural prototype. Art/rig rendering and births **not implemented**.
**Relationship:** Extends `NPC_PHOTOREAL_FACE_3D_BODY_HYBRID_2026-11-01.md`.

## Why

A child's face should plausibly resemble both parents. Siblings should be distinct and a grandchild may inherit recognizable family characteristics. Appearance must remain stable through save/reload, regional cold↔warm transitions and later 3D detail activation. Full facial image assets must not be allocated for every aggregated resident.

## Inheritance model

- 20 appearance traits × 4 simplified loci × 2 alleles per locus = **160 raw genotype bytes per individually tracked person**. Other stored metadata and SQLite page overhead add to this.
- Traits include face/head dimensions, eyes, nose, jaw, lips, ears, skin/eye/hair pigment, and hair curl. This is an aesthetic mathematical abstraction, **not a biological human DNA model**.
- Founder appearance deterministically derives from a per-world seed, a stable world/scope identifier and the existing authoritative NPC ID. Founder alleles vary around a per-trait baseline.
- Child appearance inherits one of each parent's two alleles per locus, with a small (0.8% per strand) bounded chance of change. Parent order is normalized; repeated enrollment is idempotent, cannot silently rewrite parentage.
- Face morph traits are renderer-neutral normalized `[0,1]` values. The 3D head rig must remap these to physically safe blendshape bounds. The system **does not** implement eye tracking, blink control, lip-sync, teeth, voice or face textures.
- Aging, injuries, grooming, expression and clothing are independent noninheritable state layers, not written into these alleles.
- Appearance must never determine skills, beliefs, intelligence, citizenship, social status, personality or morality.

## Persistent state and admission

- `NpcAppearanceStore` is opt-in and stores profiles in dedicated SQLite tables, partitioned by world scope and seed. Genotype is an immutable BLOB, accompanied by lineage metadata and a noncryptographic corruption checksum.
- The caller must supply an authoritative identity-checking callback. In the real `PersistentWorld` test it accepts only IDs present in `world().npc_records()`.
- Parent profiles must exist; birth/individualization authority must have admitted the child ID before creating its profile. The current game has **no real birth command**, so three-generation family stress tests use explicitly simulated, test-authorized identities.
- Data stored in the same SQLite file are **not automatically transactionally coupled** to a `PersistentWorld` world-snapshot command or regional ownership journal. These require a host-owned birth/event transaction API before live gameplay integration.
- Cohort residents remain aggregate. Only named/activated individuals obtain profiles; this avoids `O(population)` visual-genotype storage for large colonies. Appearance `exists()` can inspect a historical record while `get()` requires a currently authorized ID.

## Rendering contract and acceptance (next phase)

- Maintain a fully animated 3D body and actual 3D face/head structure; generate/fit photoreal skin materials and optional multiview view-conditioned facial detail around that identity. No flat face plane.
- Face morphological recipe is preserved through activation; material/texture IDs and later aging/appearance changes are separate persisted assets.
- Rig-friendly eye sockets, jaw and lip shapes must remain within safe ranges. Validate gaze tracking, blink timing, closed-mouth phonemes, teeth clipping, expressions and dynamic lighting in an actual game engine.
- Produce family portraits and visually evaluate resemblance/variety. Numeric trait resemblance is not a substitute for perceptual testing.

## Follow-on gates

1. Host-authenticated birth and individualization receipts with cross-process/region owner fencing and atomic appearance enrollment alongside population change.
2. Serializable, renderer-specific 3D head morph mappings and asset versioning; photoreal PBR face textures and multiview detail.
3. Real eye/lid animation, blendshape jaw/phoneme controls and speech synchronization.
4. Visual three-generation family/character studies and cold↔warm continuity tests with actual rendered characters.
5. Budget/LOD: materialize only nearby named individuals, lazy bake or cache textures, and benchmark draw calls, VRAM and GPU frame time.