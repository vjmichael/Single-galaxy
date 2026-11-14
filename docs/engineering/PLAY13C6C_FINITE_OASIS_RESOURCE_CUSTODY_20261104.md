# PLAY-13C.6C — Finite Physical Oasis Sources and Recovery

## Delivery scope

The earlier C.6B oasis prospect is a **prediction**. C.6C adds **explicitly supplied physical water and nutrient deposits** at world genesis, a native collection operation, source depletion, conservation and journaled persistence.

This release does **not** automatically populate a formerly barren or already-saved star system. It deliberately refuses to mint water or fertilizer upon first discovery, arrival, survey, restart or migration. The initial world's author must allocate measured finite site material before the first SQLite world snapshot. Planetary site geology, extraction rigs, solar arrays, soil models and operating farms remain separate future gates.

## Actual physical source model (v75)

`Simulation::provision_oasis_reserve_genesis(body,water_kg,nutrients_kg)` seeds a unique reserve only when:
- world is at genesis with no accepted or queued commands;
- authoritative physical celestial catalogue exists;
- body matches an actual **rare oasis candidate**;
- source water and nutrient masses are finite, positive, capped, and together represent whole tonnes.

Both kinds of material are included in the **existing world total_ore ore-equivalent physical mass**. The durable world resource origin, initially established when SQLite first saves this genesis state, incorporates them. Each source has its current and original kilogram counts. There are no automatic respawns or infinite background production.

The canonical `galaxy-kernel-v75` snapshot records `oasisreserve|body|water_kg|nutrients_kg|initial_water_kg|initial_nutrients_kg`. Restore rejects forged or unrecognized celestial bodies, non-oasis origins, duplicates, negative or above-original reserves, noncanonical ordering, and impossible mass. v74 worlds without explicitly supplied genesis resources remain valid and retain no hidden replacement stock.

## Authorization and extraction

`Simulation::collect_oasis_resource(owner,ship,body,water,kg)` checks physical ship and owner, outfitted exploration scanner, in-space/ready state, durable ship frame anchored to the same body, a real remaining reserve and ship life-support ledger. A deliberately bounded manual batch takes 1–10 whole tonnes (integer kilograms) and **subtracts the exact resource amount from the local reserve before adding it to that ship's water or nutrient inventory**. No food, oxygen, crew or energy is produced from the transfer. It is a prototype shipboard collection operation, not a constructed planetary mine.

`PersistentWorld::collect_oasis_resource` rejects ships in transit, clones the old world, verifies whole-world mass against the original baseline, and atomically commits through the existing SQLite journal and crash-failpoint machinery before publishing changes. A failed commit cannot leave the old world with changed material. The actual authenticated native host accepts `OASIS_COLLECT|body|WATER|1000` or `OASIS_COLLECT|body|NUTRIENTS|1000`, broadcasts a real successful collection receipt, and replays the updated source mass in `OASIS_SOURCE|body|water|nutrients`. The browser validates and displays source and receipt events. No unimplemented UI button is claimed.

**Open idempotency hardening:** a repeated host command is a *second* valid request and will debit another batch. Before automated NPC mining or unreliable network retry, bind collection to an explicitly unique durable request ID and exactly-once receipt semantics, using existing command/effect journals. Do not imply exactly-once network delivery from transaction atomicity alone.

## Native acceptance

`tests/PLAY13C_PhysicalOasisResourcesTests.cpp` verifies real rare catalogue body selection, genesis allocation, unchanged ship inventory on prospect discovery, refusal outside the active celestial frame, refusal of wrong owner and malformed/oversized requests, SQLite failpoint rollback, exact 1,000-kg physical water and nutrient transfers, constant world material baseline, strict v75 save/reload, forged-source rejection, restart, complete water exhaustion and no respawn. Registered in Windows/Linux CMake PLAY-13C workflow.

CI is **pending**, so the PR remains draft, not ready for merge.

## Next physical layers

1. Make the body resource generator part of finite galaxy **genesis**, not lazy visit-time seeding; decide whether each local water/nutrient source is geological ice, aquifer, regolith or subsurface deposit and persist actual positions/custody.
2. Require real, physically built extraction equipment, power, adequate shelter/maintenance, storage tanks, workers and safe transfer ranges rather than the current bounded manual survey-tool collection.
3. Implement planetary crops/farms using **real stored** water, nutrients, CO2, light and agricultural substrates; hauling plus compost/wastewater return must satisfy exact integer mass conservation.
4. Add authenticated durable unique collection receipts, retry-safe transfer effects, multi-vessel contention, pollution and emergency failure handling.
5. Expand native actual Windows/Linux integration and ensure asynchronous prior C.1–C.6B checks are green on exact head before promotion.

Preserve the existing rule: extraordinary oases may exist very far from established infrastructure but they are exceptionally uncommon, not guaranteed havens or automatically valuable ore deposits.
