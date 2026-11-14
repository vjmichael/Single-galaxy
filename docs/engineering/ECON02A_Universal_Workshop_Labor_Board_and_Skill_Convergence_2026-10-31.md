> **2026-11-06 STATUS / HISTORICAL BACKGROUND:** This October 31 document retains its original analysis as provenance; it is **not the current engineering work order**. The owner has now prioritized [INDUSTRY-01](PROJECT_DEVELOPMENT_SEQUENCE_20261106.md) and [the new source-grounded recipe catalogue](INDUSTRY01_FIRST_PASS_CATALOG_20261106.md) before NPC work, settlements, economies, transport, planets, society and player gameplay. Earlier suggestions of universal currency, **chits**, mandatory wages, cash markets or rate-driven item tiers are superseded by the owner's **finite resource-contribution/consumption and no-mandatory-currency** direction. Preserve original authoritative world inventories, native workforce and travel mechanics; do not turn this historical report into an implementation command.\n\n# ECON-02A — Universal Workshop, Volunteer Board, Basic Trades and Composite Skills

**Date:** 2026-10-31  
**Status:** C++17 *bounded in-memory prototype* implemented, compiled and regression tested. **NOT wired to `PersistentWorld`, committed SQLite inventories, player GUI, autonomous NPC decision-making, or NPC social-memory effects.**

## User-established design decisions

- Begin at the simplest workable level: a **colony ship** with finite NPCs, real local stocks and built-in bootstrapping refining/fabrication; no preplaced markets or industrial stations.
- **One configurable workshop/manufacturing center** can produce many different goods by selecting a recipe; do not proliferate nearly identical buildings, dozens of simulated conveyors or artificial resource-tier-locked furnaces.
- **One NPC handles one active production order at a time.** A workshop has explicit workstations; increasing capacity in later gameplay should require real space, machines, energy and materials.
- **Colony-critical work is accessible at zero initial skill**: basic mining, cultivation, food preparation, simple maintenance, safe standard factory operation and cargo work. Training affects efficiency and quality in future, not permission to keep basic society alive. Unsafe surgery, high-risk pressure systems, precision machinery etc. may need multiple verified proficiencies and/or supervision.
- **Advanced work requires multiple proficiencies**, not a single global XP/character level. e.g. ship hull fabrication = metalworking + welding; utility pumps = metalworking + mechanical fitting; future life-support repair = plumbing + electrical.
- Production follows a **configurable recipe/dependency graph**, not a mandated five-step metallurgy simulation. The player may manufacture an aluminum hull panel from aluminum stock and subparts; only split into ore -> mineral -> ingot -> sheet -> bracket -> panel when the extra logistics, job or quality step adds meaningful gameplay. For ship construction, use a bounded bill of assemblies such as hull, drive, power, control and life-support, not individually simulated screws.
- Organizational path: **volunteer board -> work board -> labor division -> labor department**. The board initially records human-posted tasks and NPC shortage proposals; proposals require approval. Delegated budgeting, matching, logistics, and independent NPC action are future stages, not benefits granted by a board enum.
- NPC motives arise from **personal needs and nearby shortages**, but motives create requests/decisions, not free goods. Later NPCs may voluntarily help, demand pay, decline jobs, or debate priorities; human player can assign work initially. No daily-login obligation.
- Economics begins with *local scarcity and exchange*, not a universal prefabricated currency. Real production, transport and accounting come before full wages, credit, minting, banks and corporations. **Chits** is a proposed colloquial settlement money name, subject to user choice; do not implement minted coins or supply a free starting market.

## Minimal value chain: what is real vs. demonstration

```
Current authoritative colony simulation (B7.0c etc):
  Geological asteroid -> named mineral extraction -> separated usable aluminum stock
                                       (real persisted material receipts)

ECON-02A prototype (standalone catalog and in-memory inventory, NOT yet authoritative):
  aluminum feedstock --[smelt]--> aluminum ingots
  ingots --[roll]--> sheets         ingots --[draw]--> wire
  sheet --[weld]--> bracket         sheet --[fit]--> valve body
  2 sheets + bracket --[assemble]--> reinforced hull panel
  wire + valve body --[assemble]--> utility pump

Future authoritative bridge:
  real local goods stock -> physically powered workshop -> worker-bound order
     -> atomic consumed inputs / produced goods / journaled work receipt
     -> GRSM attribution, firsthand observation and context-dependent opinions
```

The prototype explicitly uses **1 kg mass per base item** (hull panel 3 kg, pump assembly 2 kg), so its example recipe graph conserves mass. These are *test units*, not realistic shipbuilding dimensions or refined industrial yield data.

## Code delivered

- `include/galaxy/EconomyPrototype.hpp` — item, skill, recipe, worker, board stage, order, receipt and workshop interfaces.
- `src/EconomyPrototype.cpp` — seven example recipes, one-site/one-factory stock, approval, worker and station exclusivity, skill predicates, power and local attendance checks, active-work inputs reservation, cancellation refunds, tick-based completion and work receipts.
- `tests/ECON02AWorkshopTests.cpp` — verifies the full example material chain, mass preservation, complete work receipts with worker IDs, basic-worker inclusion, advanced two-skill gating, power/absence pauses, active-work exclusivity, shortage proposal approval, cancellation and nonrepeatable completed work.
- `CMakeLists.txt` — compile `EconomyPrototype.cpp` with the kernel and register `galaxy_econ02a_workshop_tests`.

**Important boundaries:** Current code does not implement persisted work orders, independent work-source receipts, origin ship goods, a multi-worksite storage network, finite power draw, salaries, true market pricing, autonomous labor, worker consent, quality/wear, production machinery as physical inventory, or tamper-safe recovery. `seed_stock` is explicitly test/host fixture input, **not** an authorized gameplay mint. Skill levels are provisioned as test profiles; full apprenticeship and continuing skill records are not integrated yet.

## Verification

- Local Linux CMake Debug **full build: PASS**.
- Dedicated `galaxy_econ02a_workshop_tests`: **PASS 1/1**.
- Completed non-live CTest: **42/42 PASS** (includes new workshop and existing GRSM/colony persistence tests).
- Node.js: **84/84 PASS**.
- Python design tests: **3/3 PASS**.
- **Full native CTest: INCOMPLETE**, exceeded execution window in older live relay tests after 32/67 had passed. A partial, time-interrupted run is not claimed as 67/67. Follow-up live relay hardening remains open.

See committed test logs under `docs/ECON02A_*.log` (excluding very large transient build dir).

## Near-term acceptance gates

1. **ECON-02B / GRSM-04b.B2:** Bind an actual configured workshop to a nearby **player-built outpost**, link input reservations and final output to authoritative local inventories, `PersistentWorld::step`, `resource_ledger` and physical event/worker receipts. No ordinary command can falsely credit a worker.
2. **ECON-02C:** Move named workers between ship/habitat/farm/workshop with no person duplication, persistent one-active-job schedule, skill XP earned by productive work, and explicit presence/survival. Volunteer board and shortage proposals survive SQLite restart. Beginning in `Volunteer` state must not grant labor office authority.
3. **ECON-02D:** Add real tools/workstation construction, wear, power budgets, failure/repair and batch costs. One workshop supports multiple recipe classes; machine capability constrains real output, not fictitious input tier.
4. **ECON-02E:** Introduce real named haulage orders, local stock, delivery receipts and trade requests; stock, location, money and authority conservation.
5. **ECON-02F / GRSM connection:** NPCs can personally observe committed economic events and form independent/group-context interpretations. A completed rescue/repair or shortage is not inherently a benevolent or criminal action, and no automatic universal reputation or group consensus occurs.
6. **Governance/management later:** NPC-initiated priority proposals and negotiated contracts, then appropriately scoped work-board delegation and finally formal labor institutions.

## Roblox Factory Simulator reference and deviation

- Official Roblox experience: https://www.roblox.com/games/6769764667/Factory-Simulator
- Community machine examples: https://roblox-factory-simulator.fandom.com/wiki/Machines

That game's cutter, press, furnace, extruder and assembler pipelines demonstrate understandable transformations; it also uses tier-specific inputs and progression. **Single Galaxy should borrow the legibility of the factory loop, not artificial tier lockouts or currency-by-selling-to-a-magical-infinite-market.** A universal configurable manufacturing center plus explicit recipe/tool limits better fits a resource-scarce bootstrap colony.

## Currency research companion

See `docs/engineering/ECON02A_Global_Money_Slang_Currency_Name_Research_2026-10-31.md` for cross-country slang with sources and a provisional, optional `Chits` naming recommendation. No currency has been minted by this gate.
