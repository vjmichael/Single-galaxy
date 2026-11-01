# ECON-02B2d4a — Saved blueprint to durable, material-backed construction jobs

Date: 2026-10-31 / 2026-11-01 local Linux prototype  
Parent: ECON-02B2d3b, commit `cf57acd4c3eae85c5909a1d50534b9fa28066a8c`  
Status: **Implemented and locally verified**, limited world construction-planner adapter. **Not** a factory GUI or autonomous NPC builder.

## Design rule

A canonical `FactoryLayout` blueprint is **only a plan**, and costs zero resources to save or preview. Actual equipment, grid placement, directed links, and conveyor segments are created **one step at a time** by existing host-authorized, world-journaled game commands. Every real construction step consumes existing manufactured modules, wherever required; module cost for one constructed conveyor tile remains the earlier provisional gameplay balancing rule. There are no mandatory factory-floor bounds, no payload mass/density constraints and no free equipment from importing a plan.

## New implementation

- `include/galaxy/BlueprintConstruction.hpp` and `src/BlueprintConstruction.cpp` compile a **canonical saved blueprint** into an ordered construction-job preview with per-job resource-module costs.
- `BlueprintConstruction::manifest()` saves the canonical blueprint, intended site, colony ship, owner, plan-reference number and an active/cancelled flag in a versioned text document. `from_manifest()` validates the entire document; malformed or noncanonical input fails closed.
- `inspect(const Simulation&)` reconstructs completed and unfinished jobs **from the authoritative game world**, not a mutable progress percentage. It checks real site/ship ownership, existing equipment positions, directed link identity/rate/item, and each segment's ordered path. Different pre-existing geometry or contradictory link topology makes the plan conflict, rather than silently overwriting constructed property.
- `execute_next(PersistentWorld&, authenticated_owner, authenticated_ship, fresh_command_id)` performs **at most one** ordinary authorized construction command and produces the real world event receipt. Host code must supply a fresh command ID for every attempt, including an attempt that failed for insufficient modules. The persistent command journal and material ledger are authoritative; the saved manifest cannot authorize execution.
- The supported preview/construction adapter maps: blueprint device ID **1** to the base outpost storage (node 0); IDs **2+** to additional real containers; special device ID `UINT32_MAX` to the installed **Tier 1 workshop**. New containers and the workshop can be constructed from real modules before placement. Supported links automatically resolve to the durable world-assigned command ID, allowing interrupted link construction, save/restart and retry without reusing an old action ID.
- The supported item catalogue includes `good/aluminum`, `good/aluminum_panels`, `good/aluminum_frames`, `good/aluminum_scrap` with the existing authorized transport rates. Link construction uses `ConstructStorageLink` / `ConstructWorkshopLink`, material-backed `PlaceConveyorCell`, and `CompleteConveyorRoute` only after all planned cells are installed. A partially constructed conveyor cannot transfer stock.
- `cancel()` changes the **saved user-side plan manifest** into a cancelled plan, preventing its helper from submitting any more jobs. Already constructed physical equipment/embodied materials remain in-world, without magical refunds. **Cancellation is not yet an authoritative revocation record in SQLite**; deletion or editing of a user document cannot revoke separately issued world authorization. Persistent host-managed cancellation is a future gate.
- There is no additional world snapshot/SQLite schema migration. The world uses the previously tested snapshot **v30** and existing per-command durable effect/resource receipts.

## Local acceptance

All-target Linux Debug compilation: **PASS**.  
Full CTest native/integration including legacy live/relay tests: **82/82 PASS** (4 workers, per-test timeout 18 seconds).  
Non-live CTest: **58/58 PASS**.  
Node.js: **84/84 PASS**.  
Python design: **3/3 PASS**.

The new `galaxy_econ02b2d4a_blueprint_build_tests` exercises:

1. Canonical saved blueprint and construction manifest round-trip, malformed-document rejection, unsupported junction rejection, and documented equipment restrictions.
2. Actual 3-device/2-route workshop construction: 16 authorized steps; 11 real module charges across storage, machine, links, and six conveyor cells, with zero additional cost for placement, route activation, and blueprint saving.
3. Stage eight steps, write a manifest to a separate user file, close the SQLite world, reopen it, and derive the same progress from durable authority.
4. Reject mismatched player/ship authorization, allow a real material shortage to block work without consuming modules, mine/refine/fabricate new modules, and resume with **fresh command IDs**, preserving old physical work.
5. Once construction finishes, reject replay, transfer real goods into the newly installed transport network, and conserve the original total game material.
6. A cancelled saved manifest cannot submit work; constructed items stay. Older milestones' factory, persistence, labor, networking, and live tests all pass.

## Boundaries and next acceptance gates

- **Not yet a graphical editor.** Client UI must render validated ghost blueprints and invoke `execute_next` through an authenticated host action, never mutate world state directly.
- **Not yet full universal construction.** Machine tiers 2–5, rotatable ports, filters, splitters, mergers, walls/floors, multiple dedicated workshops, arbitrary inventory types, and construction from raw ore are future work. The adapter deliberately rejects layouts containing devices that the authoritative world cannot actually construct.
- **Not yet NPC-executed building jobs.** NPC foremen may propose layouts, but execution requires future actual on-site worker scheduling, resource access and trusted job assignment receipts; no autonomous NPC authority is being claimed.
- User documents are not a secure authority source. Real plan cancellation, plan ownership, and cross-device plan conflict/revocation will eventually require durable host-owned plan registration. The current saved plan is a resume/preview convenience using world-command identity checks.
- This bounded adapter permits at most 256 devices/256 transport routes and 10,000 construction steps for safe parsing/processing. These are per-plan request limits, **not factory land or overall equipment limits**; players can submit multiple blueprints.
- Remote galaxy-scale aggregated construction/production, Windows-native installer/GUI acceptance, physical meshes and completed end-to-end interactive playthrough remain open.

**Next:** ECON-02B2d4b: an authenticated in-game ghost-plan editor and host-owned saved plans/cancellation, then on-site NPC builder queues under the same authoritative construction steps.

GitHub remains the repository, provenance, milestone and evidence record—not the development hub.
