# Single Galaxy — 1960s–1980s architectural language

**Decision recorded November 1, 2026.** Art direction, not a working renderer.

The user wants a 1960s space-age to 1980s utilitarian science-fiction architecture: believable human-made, rugged, modular and easy-to-service frontier construction, not sleek minimalist surfaces.

Use strong structural silhouettes—pressure cylinders, hemispherical modules, trusses, docking collars, tanks, pipes, radiators, braced joints and visible maintenance access. Early settlements must look **constructed from limited real resources**, not prefabricated by the game. Factories should read as expandable assemblages, without arbitrary bounded floorplans.

Interiors can use ribbed corridors, analog gauges, tactile switches, valves, machine labels, CRT/early-digital-style interfaces, subdued industrial colors and selective warning stripes. The retro inspiration is visual/architectural, not a mandate to artificially restrict future computation or modern in-universe capabilities.

**Visual continuity:** A massive irregular asteroid, major ship or station must **not visibly morph its outer silhouette** just because the player moves. Keep macro silhouette stable at intermediate distances; remove nonessential surface work, expensive materials and attachments first. For suitable far objects, prefer multi-angle 2D impostors with depth/normal information, preloaded and crossfaded rather than hard geometry swaps. Dynamic ships may retain real animated thruster/appendage geometry. Choose render tier per camera with projected size and angular error, not simply a meter cutoff.

User-built structures can require rebaking their far impostors after physical changes. Rendering LOD never creates/removes cargo, NPCs or authoritative gameplay state.

**Future art acceptance:** ship and station shape sheets; a settlement/factory construction language; asteroid camera-orbit and high-speed-approach motion tests; multiple light angles; actual Windows GPU/VRAM profiling. No art renderer or baked assets have yet shipped.
