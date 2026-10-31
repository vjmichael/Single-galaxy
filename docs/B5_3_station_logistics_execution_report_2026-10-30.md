# B5.3 — Two-Station Local Logistics: Execution Report

**Date:** 2026-10-30  
**Status:** Implementation and Linux automated verification PASS; native Windows and actual Chromium rendering NOT verified  
**Scope:** Original Single Galaxy simulator, not EVE / EVEmu

## Goal

Extend B5.2's committed local station/ship/hangar lifecycle with reversible loading, a physical trip between two separate stations, unloading, and *finite-budget, location-specific* ore sales. Ensure inventory and currency integrity during failures, restarts, and replay. Avoid introducing cross-system jumps or falsely representing instantaneous teleports as travel.

## Implemented

1. `Action::LoadOre`: at the exact docked station, move `min(request, local hangar ore, free ship hold)` into the ship. No remote-hangar access; no negative amounts, unauthorized issuer, or operations in transition.
2. `Action::SellOre`: while docked, sell local-hangar ore into that station's market stock at a preconfigured *integer* bid. Purchased ore does not disappear. Transfer station credits to owner wallet. The station can purchase only what its **finite treasury** affords, allowing a partial fill; an empty treasury rejects new sales with `INSUFFICIENT_FUNDS`.
3. **Two stations in the local visual demo**, A at `(0,0)` and B at `(12,000,0)` millimeters. A offers 2 credits/ore and B offers 5 credits/ore; each market begins with 1,000 credits. B5.2's original rock remains at `(2,500,700)` and the player's initial ship, cargo hold, and Station A hangar remain as before. This gate is deliberately **same-system**; it doesn't implement gates, warps or independent solar systems yet.
4. The visible client adds **Load**, **Sell**, and travel-assist controls. Travel assist issues ordinary authorized thrust commands to accelerate and decelerate toward a station. It does **not** mutate position directly, bypass the simulated movement rules, or publish uncommitted state. Manual thrust cancels assistance. Dock requests still enforce range, zero velocity, and transition handling.
5. Host/relay authority is unchanged: browser sends only allowlisted, schema-constrained action names through a loopback development relay. It cannot select `issuer`, `actor`, `target`, `tick`, amounts for sale/load, or wallet values. A 202 HTTP response means queued, **not** that the simulated action succeeded.
6. Version-4 canonical snapshots add station-market stock/price/credits and player wallets; loading v1/v2/v3 snapshots remains supported. SQLite schema upgrades to v3 with a `credit_origin` invariant, `credit_total` checkpoint verification, and a sale credit journal. Command and resource ledgers now cover ore loading and sales, including source/destination identities. `recover_copy` replays and verifies v4 credit state.
7. SGVIEW v1's **additive** telemetry records `W` (wallet) and `M` (market), with removal records and projection hashing on both C++ and JavaScript sides. Old B3/B5.2 recordings and tests remain readable. The viewer now displays the docked location's local hangar only, not another station's inventory under a misleading local label.

## End-to-end numerical acceptance

| Stage | Ship cargo | Hangar A | Hangar B | Market B ore | Player credits |
| --- | ---: | ---: | ---: | ---: | ---: |
| Start, docked A | 0 | 0 | not allocated | 0 | 0 |
| Mine 20 near A | 20 | 0 | not allocated | 0 | 0 |
| Unload A | 0 | 20 | not allocated | 0 | 0 |
| Load A | 20 | 0 | not allocated | 0 | 0 |
| Fly physically to B, then dock | 20 | 0 | 0 | 0 | 0 |
| Unload B | 0 | 0 | 20 | 0 | 0 |
| Sell 20 at B (5 credits each) | 0 | 0 | 0 | 20 | **100** |

The original 40 ore units persist: **20 remain in the asteroid and 20 are now owned by Station B's market**. The player's 100 credits came from Station B's treasury, which fell from 1,000 to 900; Station A's treasury remains 1,000. Thus the **initial 2,000 credits** across both markets and the player wallet are conserved. Market sales are not material sinks.

A separate market-budget test gives Station B **30 credits**: its 5-credit bid purchases only **6 of 15 units**, paying 30, leaving **9 ore in the player's Station B hangar** and no negative funds. Other checks cover A's lower 2-credit price, wrong-station attempts, unauthorized sales, invalid amounts, empty hangars, and undocked loading.

## Verified results (Linux local container)

- **CMake / CTest:** **8 / 8 suites PASS** (`docs/B5_3_ctest_output.txt`). These include the original core, persistence, client contract, mining lifecycle, B5 station lifecycle, B5.2 live client, B5.3 C++ accounting (4 new acceptance cases), and B5.3 interactive host/relay end to end.
- **Node.js:** **30 / 30 tests PASS** (`docs/B5_3_node_output.txt`). New tests replay a whole 126-frame real host trace, check market/wallet accounting, detect modified credit/ore values, and exercise simulated HUD rendering.
- **Python:** syntax compilation PASS for development relay and integration tests.
- **Transaction rollback:** injected failure between receipt/ledger writes and snapshot commit left database and in-memory state unchanged; normal retry committed once.
- **Tamper detection:** altered `credit_ledger.credit_amount` rejected during restore; altered wallet/market frames rejected by projection validation/hashing.
- **Recovery:** corrupting latest v4 snapshot rejected normal load; `recover_copy` reconstructed correct credited state on a separate file; original corrupt source remained unchanged.
- **Loopback relay:** rejects client-supplied actor/target/extra amount fields, wrong token, and cross-origin requests; two subscribers see identical committed data.

### Not tested / known limitations

- **No Windows-native MSVC build/run** and **no true browser screenshot or Chromium navigation pass** in this environment. Node fake-DOM smoke tests are **not** equivalent to a rendered-browser test.
- Single-player local demo relay; hardcoded owner 7 and one active ship. Development token is not production authentication. World is a bounded short-lived demo, not a user save-slot or hosted multiplayer world.
- Static example prices and finite reserves are intentionally **not** a market order book, economy simulator, dynamic trading, escrow, or multi-player economy. Market account ownership is simplified.
- Station A and B share one simulation system; **no stargate, system migration, or interstellar hauling** is claimed. The travel assist is development-only and not a hardened autopilot.
- Legacy v1/v2/v3 snapshots load; existing older worlds have no configured B5.3 markets or wallets unless created in a B5.3-compatible initial world. There is no retroactive auto-seeding of markets or free credits.
- Ore remains an abstract integer unit; B5.3 does not implement multi-mineral composition, refinery/recycling, geological-scale persistent asteroids, environmental scanning, NPC personnel, or world geography. Those belong to later gates and brainstorming register.
- Checkpoint size limits, saved event history and maximum demo ticks from B5.2 remain. This is a prototype rather than scalable storage.

## Reproduce

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
node --test tests/*.js
python3 tests/B53InteractiveEndToEndTests.py \
  --executable build/galaxy_interactive_host \
  --relay live/interactive_relay.py
scripts/run_interactive_linux.sh build/galaxy_interactive_host
```

Browse the development demo from the same machine at `http://127.0.0.1:8766/viewer/interactive.html` when running the relay script. Follow **Undock → Mine 4/tick → Dock → Unload → Load → Undock → Assist to Station B → Dock → Unload → Sell**. Watch actual market stock and wallet change.

## Suggested next gate

Before multi-system hauling, stabilize the physical-travel UX and add **B5.4 recovery/return journey and multi-location navigation**, or proceed to **B6 world geography and scanning**. The brainstorming document is a design register, not evidence those much larger features exist. Requirements should be selected without folding unrelated economic/NPC systems into the station-logistics acceptance gate.
