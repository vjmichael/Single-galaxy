# Single Galaxy — factory simulation decisions and next gates

Date: 2026-10-31
Status: Design register + prioritized engineering acceptance; do not confuse with implemented systems.

## Player design decisions

- **Unlimited authored factory footprint:** There are no prescribed factory-size tiers or arbitrary room caps. Physical collision, structural support, machine footprint, terrain, available resources, utilities, and construction ownership still matter. Players save reusable ghost and completed layouts; partial structures do not function as completed walls/floors.
- **Five machine tiers:** T1 2x2 (manual), T2 4x4 (mechanized), T3 8x8 (semi-automatic), T4 16x16 (automatic), T5 32x32 (integrated automation), in shared build-grid units. Machine size describes real handling envelopes; a saw cuts compatible material, not an item whitelist.
- **Minimum complete recipe batches:** Illustrative quantities are 1,4,16,64,256 recipe runs per cycle across T1–T5. A shortage of even one required ingredient blocks the entire new batch: **zero input consumption, zero product, zero scrap**. A previously reserved valid batch retains work-in-progress across recovery.
- **Quantified production:** Actual operation is bounded by material stock, all ingredient minima, machine rate, routing/feeder throughput, finite finished and intermediate stores, scrap space, power, staffing, condition, and maintenance. Output and scrap must fit in assigned stores; a blocked downstream stage creates backpressure. Large remote factories calculate batch amounts without animating every conveyor item.
- **Conserved material:** For standard eligible material-processing steps, output mass is 95% finished material and 5% recoverable physical scrap. Other operations may have specifically balanced recipe outputs; ore-to-mineral yields first obey actual composition. Scrap must go somewhere; full scrap bins can stop production.
- **Staffing:** T1/T2 nominal one operator-equivalent per active machine; higher automation reduces direct operating time but increases independent maintenance and technical-support demands. Advanced machine staffing is about capability, not character levels.
- **General-purpose utilities:** Storage, buffers, splitters, merges, filters, stack/batch feeders, conveyors, lifts/chutes, scrap collection, loading, electrical/cooling supply. Functions are inspired by factory-simulator genre mechanics but implement original gameplay and assets.
- **NPC factory planner:** Recipes express material/operation dependencies and machine capabilities; deterministic planners can generate valid generic layouts, supply quantities, utilities, build ghosts and material-backed NPC construction jobs. They use player-equivalent rules: no free stock, machines, or finished floors.
- **Initial economy:** Chit official currency; guy common slang. Aluminum is the industrial backbone, not a magical reserve backing for newly founded colonies; initial labor, barter, and real transferable claims precede any mature minting/banking economy.

## ECON-02B1 — next local engineering gate

1. Install one Tier 1 processor at an actually constructed player-owned outpost, not a phantom factory or in-memory island.
2. Deliver real material from the ship to outpost stock through authorized physical logistics. Prevent competing reservations from overspending stock.
3. Bind operation to a real on-site assigned NPC and measured utility/maintenance availability. Reject remote or non-member workers and unauthorized actions.
4. Persist machine identity, recipe, immutable input/output reservations, progress and batch effects transactionally in the world save. Restore exactly, including failure and recovery tests.
5. Enforce complete-batch minima and finite product/scrap space, including no output/scrap below minima and downstream backpressure.
6. Compare one-hour remote aggregate advancement with tick advancement under identical event schedules and verify mass accounting and idempotency.
7. Next after that: two-machine connected line with bounded transport and intermediate buffers. Only then build automated generic factory planning.

## Current verification limits

ECON-02B0 is isolated from persistent world SQLite and the physical colony inventory. It tests deterministic local State copying, not durable factory save/restart. Full live GUI/Windows E2E has not been verified. Do not label ECON-02B1 implemented until these gates pass. GitHub is repository and design/verification record, not the primary dev hub.
