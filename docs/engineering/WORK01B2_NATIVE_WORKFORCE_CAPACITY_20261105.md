# WORK-01B.2 — Canonical Workforce Qualification and Shipyard Capacity

**Status:** Source + native C++ tests + Windows/Linux workflow committed on a stacked draft above WORK-01B.1 (which is above WORK-01A). Exact-head GitHub CI and original-world acceptance must complete before calling this verified.

## Design boundary

This phase does not invent credentials, mint NPCs, open virtual shipyard slips, clone inventories, introduce a second work queue, or reassign authorized personnel. The actual entity/NPC/medical/shuttle/build and customer receipt maps in `Simulation` remain the **only** sources of truth. Work permission and worker capacity are distinct: an available yard never implies seller consent and a player's permission never confers extra construction resources.

The `WorkCapacity.hpp` header is a **read-only** typed qualification/capacity index consulted by the `WorkPlanning.hpp` native work scheduler. Both indexes are discarded and recomputed at the start of each committed world tick. Existing `galaxy-kernel-v94` snapshots remain unchanged.

## Actual qualifications (no invented shipyard experience stat)

A shipyard builder must be one of the existing two `ShipyardConstruction.workers`, physically at that ship, currently in `NpcRole::Ship`, assigned to that ship, out of transport, and pass `Simulation::npc_available_for_duty`. That existing native method already rejects medically unavailable workers and NPCs booked on real shuttle/transport operations.

A freighter commissioning NPC must additionally have approved **exact** buyer identity, colony custody, shipboard legal residence, no mentorship conflict and no concurrent active native construction/manufacturing assignment. The original native `commission_shipyard_freighter` still independently validates child guardianship, on-duty status, consumable life-support reserves, physical berth, scanner, original approval and no duplicate commissioning. Planner-qualified does **not** imply native-authorized.

Existing `NpcRecord` has farm XP but no authentic shipyard-training/credential level, so this phase does not fabricate shipyard qualifications or XP gates. If specialized technical certifications are added later, they must be canonical real records and supported by native practice/training.

## Physical slots and deterministic scheduling

For every real `ShipyardConstruction` record, the capacity inspector reports precisely **one assembly slip** once the yard is operational, never a second virtual slip. An unfinished `FreighterBuildOrder` on that yard occupies its slot. When finished, the original launched-hull receipt releases the slip; there is no new factory inventory.

Native unfinished yard construction and unfinished ship builds reserve their **original named workers** in a derived on-tick busy index. The scheduler will not select the same NPC to commission an unrelated buyer freighter during that real job. Supplier-approved but unfunded customer contracts are inspected against their actual supplier yard, existing worker qualifications and the occupied slot **before** trying the original funding gate. If busy or unqualified, they wait without any speculative debit. The original `fund_shipyard_customer_order` is still the sole physical six-module debit and queued-build authority.

A deterministic stage/age-based plan grants at most one per-tick coordinator selection per actual yard and one per named NPC across funding/commissioning. Waiting does not claim durable reservations; every tick re-evaluates the original world. Revocation stops future attempts, never refunds real already-consumed materials.

Planner `Gate` statuses newly distinguish missing/nonoperational yards, occupied slip, unavailable assigned builders and an NPC busy in real other work. These temporary distinctions map to existing durable v94 blocked codes; no retroactive snapshot rewrite. The first executor remains a buyer-authorized customer ship order. Local delivery, freight, farms, mines, crafting and maintenance are **not** newly delegated in this phase.

## Tests and limits

`tests/WORK01B2_CapacityTests.cpp` checks:
- One physical slip; active native six-module build blocks a second job; completion frees it.
- Missing, nonoperational or foreign supplier yard cannot be used.
- Assigned builders must be distinct, real, at the correct workplace, with the correct native role and allowed native duty (medical and shuttle checks delegated to the existing world).
- Same-slip same-tick contention chooses the older authorized order and makes the lower priority wait.
- Existing buyer-side construction prevents double-booking its NPCs for commissioning.
- Mentorship, physical identity, legal residence, foreign colony and transport custody failures.
- Cancellation releases temporary selection without modifying real paid work.
- Already-authorized, exactly matching original commissioning receipt requires no second crew reservation.
- Deterministic repeated planning, no mutation of NPCs/builds, and real `Simulation::npc_available_for_duty` integration.

Dedicated `.github/workflows/work01b2-capacity.yml` compiles on Windows and Linux and runs new planner qualification tests with the inherited WORK-01B.1, WORK-01A, SHIP-01F, SHIP-01B.2 and SHIP-01A real-world tests. The earlier WORK-01A SQLite failpoint/restart regression remains part of the required acceptance gate.

**Outstanding:** exact-head CI; broader workforce skill/credential system; other typed executor scheduling; emergency preemption, true multi-machine/factory capacity, configurable owner priorities, durable owner-visible wait diagnostics and real multi-owner installed-desktop acceptance. Native worker and one-slip shipyard capacity scheduling are the actual B.2 scope; an entire colony labor market is not claimed complete.
