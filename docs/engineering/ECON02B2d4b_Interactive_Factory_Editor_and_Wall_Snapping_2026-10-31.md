# ECON-02B2d4b — Interactive Factory Designer, snapped walls and canonical blueprint handoff

Date: 2026-10-31 / 2026-11-01 local engineering continuation  
Source parent: ECON-02B2d4a, `674f66c1e20f623d37ffeb220bbd4467faea800c`  
Status: Implemented browser authoring UI and native blueprint compatibility. **Not a live in-game authority.**

## Player-approved design

Single Galaxy is a game first. There are no arbitrary factory-size bounds, payload mass restrictions, density calculations, character skill gates, or construction magic. A factory may be planned anywhere within the simulation's safe numeric coordinate range; material cost applies only to **host-authorized constructed equipment**, not the unbuilt ghost plan. Wall, room and doorway pieces should snap together intuitively, especially at corners.

## Implemented

- `viewer/factory.html`: an actual interactive top-down factory designer, dark theme, with grid canvas, tool palette, inspector, throughput controls, material-cost estimates, status, keyboard undo/redo, zoom/pan and example plan. `viewer/index.html` links to it.
- `viewer/factory-layout.js`: independent, deterministic Node/browser editor logic. Validates bounded device IDs, 1×1 base/extra storage, single 2×2 Tier-1 workshop, non-overlapping footprints, compatible item ports, orthogonally consecutive conveyor cells, no self-intersections or shared tiles, source/destination adjacency, finite throughput and no directed cycles. Auto-routing finds short obstacle-avoiding routes. Errors leave the authored layout unchanged.
- `viewer/factory.js`: pointer-driven ghost equipment placement/movement, source-then-destination conveyor planning, wall/door drag, floor tiles, erasing, undo/redo, zoom/pan and save/open/export interactions. The browser shows a live estimate from the current v1 authoritative construction adapter (e.g. native fixture: 16 steps / 11 existing construction modules).
- Wall/door snapping: walls are unit-length segments of one common **integer-grid edge lattice** (`h:x:y` or `v:x:y`), and drag endpoints snap to grid vertices. Adjacent segments physically share corners/ends. Door segments replace wall segments at the same edge. Planned solid walls block new conveyor routes; adding walls across existing conveyor cells is rejected. Floors are visually marked cells.
- `.sgfactory` is a JSON **design document** including walls/doors/floors and canonical geometry; all input is validated with bounds and collision checks. `.sglayout` is the exact canonical text format used by native `FactoryLayout`, **excluding decorative overlays**. Both can be imported back into the browser (native layouts lack walls because world construction has no walls yet).
- The native test `tests/ECON02B2d4bEditorContractTests.cpp` reads the JS-produced golden fixture and validates it using the **real** C++ `FactoryLayout::from_blueprint()` and `BlueprintConstruction` compiler. No permissive parallel native parser was introduced.

## Security and authority boundary

The HTML page does not open SQLite, run game commands, create modules, assign NPCs, or authorize construction. It runs as an offline local file with no network calls, and does not offer a fake Build Now button. User-authored `.sgfactory` is not trusted source of truth. Once imported on a future live host, only the host may authenticate owner/colony ship/outpost, validate collision/resource requirements and execute each journaled job with a new command ID. Cancellation in the current B2d4a planner is a saved-document preference, not irrevocable world authority.

**Walls, doors and floors are DESIGN OVERLAYS ONLY**, not physical or airtight wall objects. Their positions are deliberately absent from `.sglayout` until an authoritative wall/floor construction API, material recipe, ownership and persistent recovery gate exist.

This interface intentionally exposes only the **currently constructible subset** of the factory domain: base storage ID 1 (1×1, 4m³), extra storage IDs 2+ (1×1, 4m³), one Tier-1 workshop ID UINT32_MAX (2×2, aluminum feed and the three supported output goods), directed conveyor routes. Tier 2–5 machines, splitter/filter/merger construction, more worker teams, gas/power layouts and unrestricted floor/wall construction remain future milestones. The underlying broader `FactoryLayout` domain supports more blueprint device types, but the durable world cannot yet instantiate them.

## Verification

- Headless Chromium (JS injected without remote navigation in controlled browser): example plan loads, canvas renders, UI metrics 3 devices / 2 links / 16 steps / 11 modules, snapped wall drag creates two new connected segments, editable `.sgfactory` is saved and re-imported with walls, native `.sglayout` is re-imported without decorative walls, and the exported `.sglayout` file is byte-identical to the C++ fixture. Browser engine emitted no uncaught JavaScript errors. Direct file:// and localhost navigation are blocked by sandbox policy here, so manual double-click launch on Windows remains a separate acceptance test.
- Node.js `tests/FactoryDesignerNodeTests.js` (9/9 dedicated, 93/93 total Node): exact canonical export; joint and corner snapping; wall/door overlay round-trip without world-item injection; source/destination/overlap checks; obstacle/backpressure-compatible paths; undo/redo, native/design imports, malformed-file handling, and rejection of new walls across existing conveyors.
- Native C++ `galaxy_econ02b2d4b_editor_contract_tests`: true canonical fixture validated and compiled to real host command plan, 16 steps and 11 material-backed module requirements. The fixture matches the JS-produced output byte for byte. Linux full-target Debug compilation passed. 59/59 non-live native tests passed and 3/3 Python design tests passed. The complete parallel legacy live run stalled after some earlier live scenarios; it is NOT counted as a pass, and Linux UI evidence does not certify Windows GUI behavior.
- Broader preexisting production, persistence, and legacy live regressions are separate gates. Windows-native browser/GUI and interactive in-game construction are **not** claimed.

## Next gates

1. Host-owned registration of imported authored plans, owner and colonization permissions, immutable plan revisions, durable cancellation/revocation and fresh construction job issuance; do not let browser JS grant itself execution authority.
2. Persisted wall/floor/door construction and recipes (resource costs, airtightness/structural affordances if any), plus collision with real terrain and existing player property. Avoid turning aesthetics into unnecessary grind.
3. Live in-game UI integration with actual outpost context and confirmed success/failure receipts; independent Windows GUI testing.
4. NPC builders: authorized work board, physically present personnel, delivery access, job progress, interruption and recovery. No free resources or hidden background autoproduction.
5. Optional later Tier 2–5, utility splitters/filters/mergers and richer no-floor-cap factories using the same route validation contract.

## Explicit non-goals for this increment

Saving or importing designs is not durable host-owned plan registration. `.sgfactory` is freely editable and shall never be accepted as authorization. Plan cancellation remains a user-document operation. The visible wall rendering is not airtightness simulation or actual construction geometry. No artificial maximum factory floor size has been reintroduced. Metric m³ item volume remains the cargo gameplay abstraction, with no payload mass limit.
