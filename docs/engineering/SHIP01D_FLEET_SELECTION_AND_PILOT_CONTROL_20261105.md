# SHIP-01D — Authoritative fleet selection and separate colony industry (2026-11-05)

**Source implemented; exact-head native Windows/Linux, host and UI acceptance are NOT YET PASSED.** Stacked on SHIP-01C, SHIP-01B.2, SHIP-01B.1 and SHIP-01A.1. This is a temporary browser interface for the future standalone PC game, not a redesign of Single-Galaxy into a browser game.

## Fundamental invariant

`home_ship` remains the founding colony resource, industry, surface work, shipyard, NPC work-board and inventory authority. `piloted_ship` is an owner-authorized **session control-plane selection**, not a transfer of equipment, workers, stock or production. A player can leave factories working on Colony A while steering an entirely different manufactured freighter in another celestial frame.

No `Simulation::canonical_state()` version change or new persistent authority ledger is needed. The underlying ship identity, crew, life-support stocks, scanner, physical position, celestial frame and manufacturing receipt already live in the authoritative SQLite world.

## Native owner-safe selection

New `include/galaxy/FleetSelection.hpp` provides a read-only player-controlled list and a safe selected ship ID.

- First entry: the original owned home colony ship.
- Further entries: real hulls with immutable manufacturing receipts whose order points back to **that owner's same founding ship**.
- Unfinished or uncommissioned hulls remain visible as **unavailable** but cannot be piloted.
- A fabricated ship is selectable only when it is a living owned `Ship`, is fully commissioned, has real `ColonyShip` + `ColonyLifeSupport` records, a paid exploration scanner, an authoritative frame and a **named currently aboard NPC pilot**. Aggregate crew count alone is insufficient.
- Foreign ships, asteroids, stale IDs, destroyed/uncrewed hulls and forged vessel selection requests fail closed.
- Every committed tick revalidates selection. If a selected hull becomes unavailable, the host automatically falls back to the original colony ship. This is a cockpit fallback, not a material/ownership handover.

The trusted native host handles `FLEET_STATUS` and `FLEET_SELECT|ship` after authenticating the owner. It publishes bounded roster receipts `FLEET_RESET`, `FLEET_ITEM`, `FLEET_DONE`, `FLEET_SELECTED`, `FLEET_FALLBACK` and per-tick `FLEET_PILOT_ITEM`. Standard `THRUST`, `YAW` and `FRAME_TRAVEL` are issued against `piloted_ship`; all other existing colony economy, manufacturing and work-board commands continue to use the unmodified `home_ship`.

Selection does not mint any new `Command` or mutate the world. Actual thrust and navigation still require ordinary owner-issued native commands, physical simulation ticks, safe transit planning and committed SQLite state. Queued motion is not presented as already executed motion.

## Secure local camera

`ServerViewBridge` optionally takes an explicit owned focus ship, and `project()` validates its owner and living ship type before revealing any local celestial region. The active cockpit sets the view focus for the next committed observation without altering game authority.

The client map, own-ship telemetry and camera follow `GalaxyActivePilotedShipId`, not whichever owned ship happens to have the smallest entity ID. The map marks **ACTIVE SHIP** distinctly from **HOME COLONY**. Focusing a freighter at another planet excludes the source colony's different physical frame and prevents remote asteroid/cargo disclosure through local-space frames. The browser does not invent or override view authority.

Mining/return automation remains explicitly **home-colony-only** in this phase; switching ship cancels old auto-assist steering so home-specific return/docking buttons cannot become commands against the freighter accidentally.

## Interactive fleet control

The temporary live interface now displays a dedicated **Your Fleet · Active Ship** panel at the beginning of the colony UI, with:

- Owned home colony and actually manufactured vessel list, ready/unavailable state.
- **Take control of ship**, **Return to home colony ship** and **Refresh fleet** controls.
- Per-selected-hull thrust directions, drift, yaw, and real celestial body travel.
- Authoritative selected ship number, heading, physical position, celestial anchor, crew and finite food/water/oxygen readout.
- A visible statement that all industry and planetary work still belong to the original home colony.
- Automatic fleet roster refresh after actual hull launch and commissioning.

The HTTP relay admits only exact-schema same-origin, CSRF-authorized `colonyFleetStatus` or `colonyFleetSelect`. The user cannot pass custom owner IDs, foreign principal strings, NPC/crew flags, location overrides, or inventory credentials through selection.

For convenience, the browser saves the previously confirmed selected ship ID in local storage. **This is merely a user-interface preference**; on page reconnect/restart it always requests the real native roster and resubmits selection through all host validation. Invalid, missing, destroyed or uncommissioned remembered IDs are rejected or discarded. The server defaults to the home ship on each host start. No raw browser preference is accepted as gameplay authority.

## Acceptance, not assumptions

New `tests/SHIP01D_FleetSelectionTests.cpp` builds and commissions a real hull from finite asteroid ore and modules in the native SQLite world, then checks:

1. Home ship is listable, uncommissioned hull and foreign ship are unselectable; fully crewed manufactured hull becomes selectable.
2. Selecting does not alter SQLite generation, world canonical text, owned crew count, resources, work board or yard assignments.
3. Camera cannot focus a foreign ship even if its entity ID is known.
4. The newly built hull travels to another real celestial body; the focused frame includes the freighter but **not** the remote original colony, and the home frame does the inverse.
5. Native projection snapshot encode/decode and SQLite restart preserve the actual hull, NPCs and commissioning receipts. Host-level selected-pilot state returns to home on restart and can only be restored by a new validated user request.

New `tests/SHIP01D_FleetHostTests.py` launches the genuine SQLite-backed native host through the authenticated transport and verifies home fleet enumeration, valid owner selection, foreign principal/asteroid denial, normal committed thrust/yaw outcome and real SQLite restart.

The Node telemetry decoder and Python relay tests reject malformed/oversized/boolean identifiers, forged CSRF and unexpected fields. Dedicated `.github/workflows/ship01d-fleet-selection.yml` runs native CTest on both Windows and Linux, re-runs the full SHIP-01C manufactured-hull Planet A → Colony B delivery and exercises Linux real host, relay and UI contract.

**Merge gate:** keep the stacked PR DRAFT until its exact HEAD reports successful Windows/Linux native fleet acceptance, host restart/authentication, browser/relay tests and full physical freight delivery.

## Later expansions

Fleet orders for multiple NPC-controlled ships, mission routing, persistent named ship identities, crew shift changes and fleet automation may follow after these fundamentals pass. Fleet selection must never be mistaken for issuing cargo authority to remote colonies.
