# Single Galaxy ART-03 — Inherited 3D Head Mesh Test

**Date:** 2026-11-01 · **Status:** Geometry/inheritance tests passed; photoreal-character acceptance **NOT passed**.

## Objective
Convert existing ART-01/02/02b persistent facial genomes into actual editable 3D head assets and establish whether three-generation family resemblance survives geometric conversion and restart. Use the unchanged 20 phenotype traits derived from the 160-byte appearance genome. This test must not fabricate births, crew, personality or intelligence.

## Implementation
- Added `NpcFaceMesh.hpp`/`.cpp`: deterministic surface geometry for skull, chin/jaw, cheeks, nose, ears, separate 3D eyeballs/irises/pupils, and static lip strips; materials from inherited skin/eye pigmentation. No texture-generation model or remote service used.
- A head assembly has **6 separately named parts, 6,150 vertices, 12,168 triangles** in its example OBJ, before any renderer-specific optimizations. Each sample exported as OBJ/MTL plus GLB using the offline Python `trimesh` conversion. The primary cranium is a closed outward-wound manifold; the lip strips are intentionally open surfaces, NOT animation-ready topology.
- This is a **geometric proof of concept**. It has no human-quality base meshes, anatomical eyelids, mouth interior, teeth, tongue, facial blendshapes, photoreal PBR skin or GPU renderer. OBJ colors/GLB vertex colors are primitive materials only.

## Acceptance and verification
1. **100 families / 700 named profiles** in the real SQLite appearance store: 100 sets of 2 parent founders, 2 siblings, a founder partner, 1 grandchild and unrelated comparator.
2. **600 generated 3D head meshes** checked for finite vertices, indexed triangles, nondegenerate triangles, part separation and geometry bounds.
3. Related child/parent mean geometric-landmark distance **0.136044** vs unrelated mean **0.230274**; children are numerically closer to parent-derived geometry. The landmark comparator is defined by the morph parameters, so this is NOT independent evidence that human viewers will recognize kinship.
4. 100 child mesh hashes unchanged after closing/reopening the SQLite appearance registry.
5. **3/3 CTest**: family birth, face appearance and new ART-03 geometry tests passed when built from the combined ART-02b source.
6. New ART-03 geometry test passed under AddressSanitizer and UndefinedBehaviorSanitizer (Linux, with leak checks disabled by the developer harness).
7. All six GLB samples reopened and each retained 6 parts and 12,168 faces; primary head + eyes + ears are watertight, while lip surfaces are not.
8. Real first-render visual review: **FAILS photorealism and animation quality**, with mannequin-like facial anatomy and static eyes and lips. An actual character art/rigging pass is required before any gameplay use.

## Reproduction
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target galaxy_art03_face_geometry_tests galaxy_npc_appearance_tests galaxy_npc_family_birth_tests
ctest --test-dir build -R 'galaxy_(art03_face_geometry|npc_appearance|npc_family_birth)_tests' --output-on-failure
# Export example family OBJ + MTL:
./build/galaxy_art03_face_geometry_tests ./art03_asset_output
```
The optional preview renderer `tests/render_art03_preview.py` uses VTK and trimesh in the development environment; GLB export uses trimesh, while the C++ generator writes plain OBJ/MTL itself.

## Remaining gates
- ART-03b: production-quality head topology built around a consistent deformable 3D base face; broader face-shape diversity; human evaluation of kinship across ages, genders and lighting. Face traits must never imply aptitude or personality.
- ART-04: eye orbit, eyelid contact, blink and gaze, jaw and lips, phoneme-viseme shapes, teeth/tongue occlusion and temporal blending. Test actual dialogue close-up.
- ART-05: AI-assisted multiview face texture/PBR workflow; consistent identity under rotation and lighting; runtime GPU/VRAM/profile tests. The "2D trick" remains experimental.
- Not tested: actual graphical Single Galaxy renderer, live `PersistentWorld` face spawn/LOD, Windows, full-project regression, high-density crowds, million-NPC activation.

GitHub is an archival source repository; this local build and inspection is the development/test authority.