# Single Galaxy — 1960s–1980s space-industrial architecture direction

**User decision, 2026-11-01.** Art direction, not new gameplay or renderer code.

## Intent

Single Galaxy is a player-built frontier with visibly engineered, repairable facilities. Its architectural language should echo 1960s space-age futurism, 1970s industrial science fiction, and practical late-1970s/1980s spacecraft/control hardware, rather than glossy modern minimalism. It is a fictional technology lineage, not a constraint that every future device must behave like 1980s hardware.

## Shape and function

- Strong readable masses: cylinders, spheres, capsules, corrugated modules, planar armor, trusses, launch towers, external tanks, radiators, piping and robust docking collars.
- Small initial settlements must look assembled incrementally from salvaged or fabricated sections. No pre-built megastructures; each permanent asset comes from real materials, labor and player/NPC construction.
- Structural seams and bolted joinery expose how objects are assembled. Access to valves, hatches, lift points and service corridors matters as much as decorative greebles.
- Different levels of industrial sophistication can coexist: improvised starter infrastructure and later high-throughput integrated factories share a legible mechanical vocabulary.
- Industrial interiors favor pressure doors, ribbed corridors, modular cabinets, high-contrast labels, analog gauges and tactile controls. Early digital panels, phosphor/CRT-like readouts, amber/green monochrome or constrained-color terminals may coexist with newer interfaces.
- Use off-white, warm gray, aluminum, faded orange, olive, dark teal, safety yellow and limited warning-red accents as a starting palette; factions, ownership, and environment can vary these. Wear and patina should follow plausible exposure, not random grunge.

## Important visual continuity rule

Large asteroids, ships and outposts must not perceptibly reshape their outer silhouette while moving between graphic LODs. Favor retained macro meshes, cheaper invisible details and multiview far-distance impostors with normal/depth channels. A giant irregular asteroid should not 'melt' into a visibly different polygon shape during approach. Use high-speed flyby, zoom and camera orbit acceptance video later.

## 3D content budgets and art implications

- Make the same silhouette function at short and medium viewing distances; author major hull and boulder contours as stable geometry, not as discontinuous decimated LOD variants.
- For constructed factories, emit component-level geometry from the player's actual blueprint and separately bake far-distance impostors of **completed, stable** aggregates. Incomplete or modified stations should use direct geometry until a new validated impostor is available.
- Dynamic ships can keep minimal real geometry for articulated parts, thrusters, docking and shadows while replacing stable distant hull surfaces with view-dependent impostors.
- Prefer screen-space criteria, temporal crossfade and prefetch over arbitrary distance thresholds. Rendering LOD never modifies NPC actions, collisions, cargo or simulation ownership.

## Validation later

Produce a concise art bible with shape sheets, floor-plan examples, palette/color tests and side-by-side motion clips of asteroid/station transitions. Determine the renderer/content pipeline first; this document does not claim texture baking, impostor shaders or user-facing art assets exist today.
