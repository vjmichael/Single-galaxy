# PLAY-13D.4E — Universal transport of people and goods (2026-11-05)

## Player-facing goal

A player gives a pickup/delivery instruction describing **which existing vehicle**, **which source and destination**, and **what physical things to move**. The same transport language must support named NPCs, finite resources and eventually equipment without turning transport into a parallel inventory or a separate NPC migration system. The player still builds ports, factories, vehicles and extraction facilities or explicitly delegates their construction.

## Implemented shared transport contract

`include/galaxy/UniversalTransport.hpp` provides the canonical, typed **goods manifest**. Goods have `GoodKind::{Npc,Resource,Equipment}` and an identity, amount and cargo mass. Named NPCs consume passenger seats, not resource kilograms; water, food and nutrients are finite kilograms and share a 1,000 kg shuttle hold. Future equipment has a distinct typed category, but requires a host-backed physical item adapter before movement is allowed. Canonical sorted lines reject duplicates, zero or unknown goods, overcapacity, pilot/passenger aliasing, bad mass and invalid endpoints.

`transport::Instruction` specifies player/vehicle identity, typed pickup and delivery endpoints, an ID for the proposed instruction, and canonical goods. `transport::serves` validates that this vehicle and route can service the order. **This is a one-step command contract, not yet a durable recurring job queue.** The current order ID is a command reference for validation; it is not a persisted exactly-once receipt or authorization capability.

`Simulation::planetary_transport_manifest(site)` constructs a read-only mixed NPC/resource manifest **from the existing authoritative planetary shuttle and its actual named NPC passenger roster**, with a stable per-shuttle custody-derived carrier ID. No shadow cargo stocks, NPC copies, or inventory tables are created. The view reflects direction (orbital vs planetary berth), transit phase, pilot, seats, cargo capacity, prior/request receipt, and real contents. Flight completion alone never implies delivered cargo; only a committed berth unload changes destination inventory.

`execute_shuttle_transport_instruction` is the first real vehicle adapter for that universal instruction. It resolves an authenticated player and authoritative player-built site, requires the correct physical berth and non-transit vehicle phase, and dispatches a single named NPC or resource line to preexisting **atomic `PersistentWorld`** booking, boarding, disembarkation, reboarding, load or unload commands. Unauthorized or unsupported equipment orders fail closed. The native `OrbitalShuttleCommands` entry point now uses this shared adapter for ordinary boarding, disembarkation, reboarding and water/food/nutrient cargo operations. Orbital `RELEASE` remains a cancellation, not an invented transport-delivery receipt.

The universal transport is deliberately a **contract/adaptation layer** over current physics, ownership, NPC custody and SQLite journaling. It does not grant custody, authorize migration, mint stock, invent infrastructure, or silently move goods between distant facilities.

## Verification coverage

`PLAY13D_UniversalTransportTests.cpp` covers canonical mixed manifests, type/units, seats, mass, origin/destination, wrong owner/vehicle, duplicates and unsupported resource IDs. `PLAY13D_RegionalFacilitiesTests.cpp` exercises the native adapter against a real constructed shuttle and planet, including named passenger manifests, resources, wrong owner, unsupported equipment, finite nutrient recovery, SQLite rollback and restart. `PLAY13D_ShuttleHostFixture.cpp` checks a reconstructed mixed manifest after a native SQLite restart. Both Windows and Linux are registered in `.github/workflows/play13d.yml`.

## Deliberately deferred: autonomous hauling orders

The next gameplay-facing increment should save **one finite player-issued transport job** (pickup site, destination, resource/NPC reference, requested quantity, assigned real carrier/pilot), then let the host advance actual existing movement/berth actions across ticks, with cancellation, resource shortages and recovery. No infinite auto-build loops, no free landing, no phantom NPCs, and **no automatic permanent NPC immigration**. General equipment hauling requires a real equipment-storage adapter. Until that job ledger is implemented and tested, the completed API means **shared manual/native pickup and delivery steps**, not automatic dispatch between colonies.
