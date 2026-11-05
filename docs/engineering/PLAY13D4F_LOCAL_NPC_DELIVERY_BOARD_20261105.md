# PLAY-13D.4F — Local NPC Delivery Work Board

## Gameplay rule

**The player posts a simple order; NPCs do the job.** There is no NPC contracting economy, wage model, application process or bureaucratic approval flow. A local board belongs to the player's existing colony ship and contains finite transport jobs, not phantom stock.

Desired contract example:
> Collect 500 kg of nutrients from Planet A and deliver them to Colony B.

The universal goods instruction already supports typed goods and endpoints. The **first executable route** is intentionally smaller: **a real player-built Planet A spaceport → its parent colony ship in orbit.** Delivering from that ship to a separate Colony B needs a second physical vehicle/route adapter. The board must not silently teleport goods to the ultimate destination.

## Implemented v88 MVP

- `LocalDeliveryOrder` is stored in the canonical Simulation roster, keyed by a player-supplied unique order ID. Fields: owner, local board ship, source planetary site, resource type (water/food/nutrients), quantity 1–1,000 kg, assigned physical NPC pilot, progress state and creation tick.
- One active order per actual shuttle/site prevents simultaneous overlapping uses of the same vehicle. Completed and cancelled order IDs remain in durable history, preventing silent replay.
- Posting/cancelling are owner-authenticated `PersistentWorld` mutations, committed to the **existing SQLite world journal before publication**. Injected failures must not create partially posted/cancelled jobs.
- After posting, the host's deterministic world-step scheduler uses the **existing shuttle**. If there is no pilot, an available named local ship NPC can be assigned using the normal existing pilot-authority check; no new NPC is spawned. The shuttle must actually exist and have remaining fuel, room for the cargo and a built, operational destination spaceport.
- State machine: `Posted → Descending → Loading → Ascending → Delivering → Complete`. On each committed world step, the simulation checks the real location/arrival receipt, physically loads the requested resource from the surface stock, flies back, and unloads into the parent ship. All flight durations, NPC custody rules and conservation checks come from existing code. If stock, carrier access, fuel or pilot is unavailable, work pauses instead of inventing transport.
- The player may cancel an unstarted `Posted` contract. **Once a flight begins**, cancellation cannot erase the traveling pilot/cargo or bypass landing and recovery. No automatic reverse transport is implied.
- Auto-flight request IDs live in a stable disjoint range below JavaScript's maximum safe integer: `0x10000000000000 | (order_id << 1) | return_leg`, with job IDs below `2^50`. Existing single-use flight request checks still reject collisions and replays.
- v88 canonical snapshots append a bounded `localdelivery` record without introducing a second inventory table. v1–v87 worlds remain readable until a player posts a contract. Snapshot parsing checks owner, actual shuttle/site, source ship, valid resources, nonzero IDs, one active job per site, real named courier and flight-phase consistency.
- Local native host commands are `LOCAL_WORK_POST|body|lat|lon|resource|kg|id`, `LOCAL_WORK_STATUS`, `LOCAL_WORK_CANCEL|id`. The authenticated local relay exposes `colonyLocalWorkPost`, `colonyLocalWorkStatus`, `colonyLocalWorkCancel`; the existing planetary UI provides three controls. The host emits `LOCAL_WORK_ITEM` updates after committed progress changes.

## Acceptance

- Extended `PLAY13D_RegionalFacilitiesTests.cpp`: a player creates a live nutrient pickup order, the already named shuttle NPC executes both finite flights and transfer operations, source stock falls, ship stock rises, whole-world mass is conserved, and the finished job survives SQLite restart. Also checks wrong owner, duplicate active route, posting/cancellation rollback, resource shortages, posted-only cancellation and replay rejection.
- Native relay and Node tests validate typed commands, owner/session checks and progress telemetry.
- Windows/Linux PLAY-13D native workflow covers these existing targets. **Do not claim native pass until the exact head SHA has completed successfully.**

## Deliberate limits

This is a single local **shuttle-supported pickup contract**. It is not yet a general multi-hop freight network, passenger migration work board, route planner or equipment carrier. The next increment extends the same work-order schema to real onward cargo ships and registered colony inventories, including multiple legs and cargo reservations, without copying resources or adding unrelated NPC social mechanics.
