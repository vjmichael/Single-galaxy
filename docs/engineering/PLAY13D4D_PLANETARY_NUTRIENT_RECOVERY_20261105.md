# PLAY-13D.4D — Player-directed planetary nutrient recovery

## Goal

Give the player a tangible reason to establish and maintain a planetary extraction site: **finite nutrient stock**, extracted from a surveyed local oasis and recovered using an existing player-built, NPC-piloted orbital shuttle. No new travel network, NPC migration rules, automatic construction, or direct player surface walk-around is introduced.

## Implemented gameplay chain

1. The player discovers and surveys the finite oasis region, then authorizes construction of the surface spaceport. Named NPCs consume real finite materials and time to build it.
2. The player constructs the extractor and separately fabricates the shuttle. Neither appears because of NPC autonomy; both require player commands.
3. The player directs nutrient extraction (manual command or explicitly set site workforce policy 3). Production draws kilograms from the existing `oasis_reserves_` and accumulates in `PlanetarySiteFacility::nutrients_kg`.
4. The player moves the colony ship to this celestial body, sends the shuttle down, and loads `NUTRIENTS` **at the landed berth** from the real planetary site's stock. A 1,000 kg cargo limit is shared with water and food.
5. The NPC-piloted shuttle returns to orbit. Only then can the player unload the nutrients into the ship's existing `ColonyLifeSupport::nutrients_kg` inventory. The ship's inventory cannot collect the surface materials through the shuttle while the shuttle is en route or at the wrong berth.
6. Player may depart with recovered materials, or assign further NPC operations. Workers do not need to commute home.

## Implementation boundaries

- `ShuttleCargo::{Water, Food, Nutrients}` extends the **same** ship/site and durable SQLite `PersistentWorld` transfer path. The two legacy `bool water` overloads remain for existing callers; desktop shuttle commands use typed nutrients and preserve water/food defaults.
- Canonical snapshot `galaxy-kernel-v87` appends `nutrients_kg` to the shuttle manifest after the existing per-visitor receipt section. v80–v86 records remain readable and unchanged until a nutrient cargo transfer upgrades the world. Whole-world conserved mass includes held nutrients. Loading, unloading, shuttle-flight consumption and source extraction preserve resources; no nutrient synthesis occurs.
- Native `PLANET_SHUTTLE LOAD/UNLOAD` accepts authenticated `NUTRIENTS` cargo. Relay input is strictly typed; native host independently validates it. The existing UI adds a nutrient cargo option, labels physical-berth transfers clearly, and displays the actual cargo and planetary stock.
- The existing shuttle and site telemetry formats optionally append nutrient kilograms (rather than changing old positional fields). Legacy consumers still receive unchanged frames for zero nutrient stock, and current viewer accepts both old and extended frames. This is an implementation compatibility bridge, not a general arbitrary-item cargo protocol.
- Regional facilities native test covers a complete extraction → physically returned player ship → shuttle descent → surface load → ascent → orbital unload sequence, source/ship stock checks, wrong owner/frame/berth, capacity denial, injected SQLite snapshot-write failure, v87 round trip, world mass conservation and restart.
- Live native shuttle/relay E2E test checks nutrient command acceptance, extended cargo status, complete zero-payload status, and persisted native host restart. Node tests cover both v87 and older UI frames.
- **Note:** preexisting direct `PLANET_RETRIEVE` support for food/water predates this feature and remains available for compatibility. This gate adds proper shuttle recovery for a distinct planet-sourced product; it does not claim to have removed every old abstract transfer.

## Acceptance still required

Exact-HEAD C++17 Windows/Linux build and the full native PLAY-13D test workflow must pass. Do not claim native success from source changes or JavaScript syntax-only checks; inspect and repair any resulting CI failure. The standalone desktop UI should eventually expose a clearer guided transfer sequence and automate approach to player-built spaceports, without free infrastructure or NPC immigration.
