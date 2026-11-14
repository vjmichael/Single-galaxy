# SHIP-01B.2 — Real manufactured freighter commissioning (2026-11-05)

**Status: Implemented in draft branch; exact-head native C++/SQLite, Windows, and full end-to-end acceptance NOT YET VERIFIED.**  
**Stacked on:** SHIP-01B.1 PR #53 (first physically manufactured hull), SHIP-01A.1 PR #52 (real yard), and PLAY-13D.4G freight PR #50.  
**Version:** v92 snapshot introduced only when first commissioning commits, with v91 and earlier readers preserved.

## Actual world transfer

The builder controls an already-manufactured, still uncommissioned real freighter. To commission it they specify:
- a completed real freighter build order ID;
- two different existing named NPCs from its actual constructing colony ship (pilot and engineer);
- equal amounts of **real food, water, and oxygen** to board from that same ship, between 10 and 1,000 kilograms of each.

The source ship and manufactured hull must be simultaneously owner-controlled, physically stationary, in space, ready (not in celestial transit), in the same authoritative local frame and within the real native berth proximity rule. An unlaunched hull, another player's hull, an already commissioned ship, a remote or moving ship, a ship without an available real fabricated construction module, a ship without spare crew and supplies, or an active voyage fails closed. Shipyard builders cannot be quietly taken off duty. Both requested crew IDs must refer to available on-site ship-duty NPCs without an active transport assignment, and cannot be guardians of dependents who would be left behind.

One native Simulation change converts **one existing source construction module** into an installed exploration scanner on the new freighter, moves **exactly two named individuals** (home ship, physical location, job site and explicit ship residence) from source to freighter, and moves the exact three requested life-support stacks. The source has two fewer crew, one fewer module, and less food/water/oxygen. The new freighter has a canonical two-person `ColonyShip` operating roster, a physically fitted scanner, a 100 m³ hold already minted by SHIP-01B.1, and only the transferred provisions. Its original hull/build identity remains unchanged. **No new NPCs, cargo, insurance stocks, materials, fuel, or equipment spawn.** At least one living adult remains aboard the source colony ship with a bounded stock reserve.

Commissioning is not a repair of a freighter on another side of a solar system: no remote boarding or cargo teleport is admitted. It is a short berth transfer under one committed world state.

## Durable receipt and checks

`FreighterBuildOrder` gains immutable `commissioned_tick`, `commissioning_crew[2]`, and `provisions_kg_each`. Snapshot v92 serializes these at the end of each `freighterbuild` line; prior v91 records receive zero defaults when upgraded. The parser rejects incomplete/duplicated/invalid nonzero receipt fields, commissioning before hull launch, future timestamps, nonexistent operational crew/stock on a launched ship, or an inconsistent fitted scanner state.

`PersistentWorld::commission_shipyard_freighter` uses the same verified copy -> validate -> one SQLite commit -> swap path as construction and freight, checking `total_ore()` against the exact preexisting conserved world total. Failed admission changes nothing. A thrown `AfterSnapshotWrite` leaves both source and freighter untouched, with no generation advance. Restart restores the same NPC IDs and material receipts without a second transfer.

Native bridge:
- Command: `FREIGHTER_COMMISSION|build_order|npc_pilot|npc_engineer|kg_each`.
- Status: `FREIGHTER_COMMISSIONED|build_order|hull_id|npc_pilot|npc_engineer|kg_each|generation`.
- Strict HTTP action: `colonyFreighterCommission` with exact keys `order`, `firstNpc`, `secondNpc`, `provisionsKg`.
- Temporary client has manual named-NPC, provision and order inputs and shows the post-commit actual ship ID.

The existing `Simulation::submit` and execution guards deny pilot commands to **uncommissioned** manufactured hulls. Once it genuinely has onboard crew, those same guards allow its ordinary owner-directed navigation, and the already existing PLAY-13D.4G freighter adapter can consider it a real, physically stocked and fitted carrier.

## Verification added, outstanding

- `galaxy_ship01b2_commission_freighter` native CTest builds a real shipyard, manufactures one hull, fabricates scanner input, rejects unauthenticated/duplicate/builder/understock requests, injects SQLite write failure, commits named boarding, round-trips the v92 snapshot, verifies exact NPC and food/water/oxygen/material counts, reopens SQLite, and submits a genuine freighter thrust command.
- Relay accepts the exact owner-scoped action and rejects boolean IDs, duplicate crew, insufficient/excessive provisions, extra fields and forged sessions.
- JS decoder checks the real commissioning event and rejects invalid/fabricated telemetry.
- **None of these tests has yet been executed on the exact head in this conversation.** No native/Windows acceptance is claimed.

## Remaining gates

1. Run exact-HEAD Linux and Windows C++/SQLite build and CTest (including earlier SHIP-01A/01B tests), HTTP/JS and failure/rollback/restart matrix. Resolve any compile or test failures against that exact code.
2. Exercise the real newly commissioned hull as the freight carrier with Planet A surface pickup -> source transfer -> physical navigation -> Colony B destination receipt. This is **SHIP-01C**, not yet proven in a full test.
3. Extend commissioning for approved dependent-inclusive family migration, damage and rescue of a commissioned wreck, and independent logistics personnel without assuming a nonexistent legal transit bridge. Current admission **rejects** moving a guardian who has a registered dependent.
4. Fully independent powered orbital slips, cross-colony shipbuilding commissions, seller/shipyard economics, NPC shipbuilding delegation and expansion remain SHIP-01D/E.

**Do not merge or state gameplay complete until exact-head native verification and SHIP-01C real-delivery acceptance.**
