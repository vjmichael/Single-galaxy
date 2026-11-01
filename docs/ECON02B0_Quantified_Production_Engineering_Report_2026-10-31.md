# Single Galaxy — ECON-02B0: Quantified Production and Hard Backpressure

**Date:** 2026-10-31  
**Status:** Implemented and locally tested **standalone production kernel**, not yet connected to the persistent world.  
**Predecessor:** GRSM-04b.B2a named shipboard fabrication + ECON-02A in-memory workshop.  
**Design direction:** No arbitrary factory-floor caps; five *machine* tiers; flexible construction; operation-based recipes; quantified remote simulation.

## New locked design rule

> If any required input is below its machine-scaled minimum, the machine does not start the batch, does not consume any of the partial input, creates **zero output**, and creates **zero scrap**. A stock shortage may leave a previously valid, reserved batch in progress, but never creates a new batch from partial inputs.

The same no-output rule applies if the batch's output or scrap destination cannot accept the *entire* batch before work begins. When a batch already started and its destination fills due to a later third-party delivery, the batch stays represented as work-in-progress and waits for space before depositing outputs.

## Implementation

Added `include/galaxy/QuantifiedProduction.hpp`, `src/QuantifiedProduction.cpp`, and `tests/ECON02B0QuantifiedTests.cpp`; registered kernel source and CTest target in CMake.

- A `Recipe` specifies named ingredient quantities **per recipe run**, general-purpose `operation` name, finished output, and recoverable scrap.
- An installed `Machine` selects one of five tiers. For comparability, each machine cycle requires 1, 4, 16, 64, or 256 **complete recipes** respectively; basic cycle duration is 5 ticks at Tier 1 or 6 ticks at higher tiers. These are design-balancing examples, not game-economy production guarantees.
- A recipe for processing material must conserve input mass and issue exactly 5% of input as separately stored scrap; legitimate assembly recipes may specify their own mass-balanced scrap ratio. Fractions are represented using integer grams rather than floating point to prevent rounding inflation. This is an initial balancing policy, not the final metallurgy/material-quality system.
- Stores have finite physical mass capacity; all deposits/withdrawals are checked for validity and integer overflow. Every batch requires enough **of each distinct ingredient** and enough finished-goods **and** scrap storage.
- Nonzero partial progress reserves only a **complete valid batch** into an explicit `reserved_inputs` WIP state. Output and scrap are created **only on completion**. WIP survives a copied/restored state; an invalid reservation fails closed on construction. Power, staff, and maintenance gates pause work without making free items.
- `advance(ticks)` advances ordinary production in aggregated whole-batch arithmetic rather than one tick at a time when possible. Timed partial batches remain explicit. A deterministic 1000-tick batch run agrees with 1000 one-tick runs given the same initial state and no intervening external events.
- `total_mass_grams()` includes stored feedstock, finished goods, scrap, and WIP mass, so conserved material does not disappear when work is interrupted.
- No recipes operate on item ID whitelists: machine capability is matched to operation, not to a named output item or player level.

### Example batch

Two ingredient requirements: 12,000 g aluminum + 8,000 g sheet = 20,000 g total. One Tier 1 `cut` cycle yields 19,000 g panels + 1,000 g recoverable scrap. With only 7,999 g sheet, **nothing moves and nothing is produced** even across a million simulation ticks. A Tier 2 standard cycle needs four times all individual quantities, not four times only the total mass.

## Local verification (2026-10-31)

- CMake configure + full Linux Debug **all-target build: PASS**.
- New ECON-02B0 dedicated CTest: **PASS**.
- Non-live CTest suite excluding `_live_`, `_relay_`, `_cli_e2e`: **44/44 PASS**.
- Node tests `node --test tests/*NodeTests.js`: **84/84 PASS**.
- Python design tests: **3/3 PASS**.
- The older complete live end-to-end suite was **not rerun to completion**. No Windows-native game or multiplayer benchmark was run in this increment.

## Critical limitations / next gates

1. **Not yet PersistentWorld integration.** The stores and reservations here are owned by a standalone line prototype. They do not reference the authoritative colony ship/outpost inventory or durable effect journal. No claim of shipped in-game manufacturing.
2. **No shared storage arbitration or concurrent production-line transactions.** Multiple workers/factories/haulage competing for the same bins require an authoritative ledger and atomic allocation/reservation, with idempotent command identities and consistent recovery.
3. **No actual conveyor throughput or material transit yet.** Future link graphs must enforce transfer rates and travel delays, including cross-colony hauling; never teleport goods.
4. **No production planner or spatial construction yet.** Recipes expose operations, but autonomous factory layout, construction ghosts, validation, access and utilities are upcoming work.
5. **No machine wear accounting/maintenance scheduling here.** Only a `maintained` gate is represented. Crew assignments, fractional operator-equivalents, job queue, and power budgets must link to real workforce and utility state.
6. **No financial issuance or market transactions.** The `Chit` is the official unit; the `Guy` remains common slang. Initial colonists do not inherit free backing reserves.
7. **No authoritative save format for this line yet.** Copying a `State` validates structural WIP in a test, but does not provide SQLite journaling, simultaneous inventory effects, adversarial validation of a shared journal, or crash-atomic checkpoints.
8. **No multi-product cycle/quality/byproduct complexity.** Add further outputs, scrap routing, recycling, alternate materials and operation-specific tolerances only when tied to actual recipes.

**Recommended next milestone:** ECON-02B1 — connect one Tier 1 material processor at a **real player-built outpost** to the persistent local stock, assigned on-site NPC, actual utilities, protected stock reservations and SQLite save/restart with journaled, mass-conserved movements; then connect output destination backpressure and expand to a two-machine conveyor-linked line. Do not start with an NPC generative factory planner before these physical-economy gates work.

**Development discipline:** tested locally; GitHub remains repository/documentation, not development hub. No GitHub write in this increment.
