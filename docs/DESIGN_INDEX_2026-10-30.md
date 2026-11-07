## Current engineering checkpoint — 2026-11-06 (stacked draft work)

For the **current SHIP-STATUS/ship cyber and hailing program**, consult [the actively maintained task and verification ledger](engineering/ACTIVE_WORK_AND_ACCEPTANCE_2026-11-06.md) first, then [the hardcoded hail anti-harassment contract](engineering/SHIPSTATUS07E_HAIL_ABUSE_ESCALATION_20261106.md), [authenticated case desk](engineering/SHIPSTATUS07D_AUTHENTICATED_HAIL_SECURITY_DESK_20261106.md) and [original cyber incident evidence/stealth](engineering/SHIPSTATUS07_CYBER_INCIDENT_INTAKE_20261106.md). The older milestone sections retained below are historical provenance, **not the active completion summary**. Old review threads #78–#85 are closed as superseded; #86 is the **single active consolidated draft**. Native Windows/Linux acceptance is still unverified; Windows **installed** acceptance is intentionally deferred until real 3D assets/UI. See [the next-wave technical report](engineering/SHIPSTATUS_NEXTWAVE_COMMS_REVIEW_COMBAT_20261106.md).

---

## 2026-11-05 — First manufactured cargo freighter (SHIP-01B.1)

[SHIP-01B.1 — first real freighter build](engineering/SHIP01B1_FIRST_PLAYER_MANUFACTURED_FREIGHTER_20261105.md): one finite manufactured hull from six actual construction modules and a completed local yard, with named NPC fabrication, twelve game ticks, v91 immutable ship ID, native SQLite journal, and no free crew or scanner. **Code committed, 21/21 source-based UI tests passed, exact-head C++ and SQLite acceptance pending.** Ship commissioning, powered orbital slips, and intercolony shipbuilding contracts remain open.

---

## 2026-11-05 — Shipyard economy design (future work)

**SHIP-01 decision:** [Player-built shipyards, NPC shipbuilding, and intercolony orders](engineering/SHIP01_PLAYER_BUILT_SHIPYARDS_AND_COLONY_SHIP_ORDERS_20261105.md). The player must construct functioning shipyards using real materials, NPC labor, power and time. Authorized NPC managers may operate production and accept other colonies' orders; expanding yard capacity must require a real separately authorized build and finite inputs. **SHIP-01A.0 preflight locally passed on v66. SHIP-01A.1 construction is now coded on a stacked draft PR (v90; exact-head native acceptance still pending). Dedicated orbital yard, powered machinery and freighter production remain open.**

**Freight dependency:** [PLAY-13D.4G real carrier handoff](engineering/PLAY13D4G_REAL_INTERCOLONY_FREIGHT_20261105.md), currently draft PR #50, still awaiting exact-head native Linux/Windows acceptance. It uses an existing crewed colony ship as the cargo vessel; this is not yet a player-built dedicated freighter. SHIP-01 must not be counted as a prerequisite to verifying PR #50's narrower transport gate.

---

# Engineering milestone index (current)

**B7.0e:** [Player-built greenhouses and staffed agriculture](B7_0e_Player_Built_Agriculture_Managed_Crew_Report_2026-10-30.md), implemented and Linux-tested. Later social/faction mechanics remain design only.

# Single Galaxy — Documentation Index (2026-10-30)

- **Current binding game vision and feature status:** `SingleGalaxy_Consolidated_Feature_Spec_2026-10-30.md`. Separate **implemented** B7.0b mechanics from planned designs.
- **Current tested engineering milestone:** `B7_0b_Colony_Visual_Geology_Report_2026-10-30.md`. Test evidence and unverified limitations must not be rewritten.
- **Legacy design record:** `SingleGalaxy_Brainstorming_Ideas_2026-10-30.md` (external to source archive). Historical milestones in that document are superseded.
- **Economy reference workbook:** `SingleGalaxy_Goods_Production_Life_Gradient_2026-10-30.xlsx` (separate deliverable). Its recipes/quantities are design assumptions, not the current executable game economy. The latest game rule makes aluminum **abundant, inexpensive and efficiently processable** while requiring it in huge industrial volumes.

**Next development slice:** B7.0c real mineral-specific cargo/refining → physically usable manufactured goods → agriculture and basic NPC quality-of-life → explainable colony survival/livability/self-sufficiency and managed absence.

**Design boundary:** no mandatory starting gate or prebuilt station; NPC autonomy and politics are longer-term; no forced login chores; no rigid player XP/unlock treadmill.

## Implementation refresh — B7.0c

Current delivered engineering report: [B7.0c Mineral Industry](B7_0c_Mineral_Industry_Report_2026-10-30.md). The consolidated feature specification remains the design authority; current milestones B7.0b and earlier are historical evidence. B7.0c implements named minerals, physically conserved refining inventories, functional structural modules, journaled recovery and local Matrix Vision controls. Farming, NPC welfare and automated colony progression remain later work.

- [SHIP-01B.2 — real freighter commissioning, v92 draft](engineering/SHIP01B2_REAL_FREIGHTER_COMMISSIONING_20261105.md) — exact-HEAD native acceptance remains open; no free crew, scanner, or provisioning.

- [SHIP-01C — first manufactured freighter actual intercolony delivery, test draft](engineering/SHIP01C_FIRST_MANUFACTURED_FREIGHT_DELIVERY_20261105.md) — real built hull, named crew, planetary work order, verified-when-passed physical freight.

- [SHIP-01D — owner-safe fleet selection and separate colony authority, draft](engineering/SHIP01D_FLEET_SELECTION_AND_PILOT_CONTROL_20261105.md) — real hull roster, active cockpit, native local-frame camera, safe home/flight separation.

- [SHIP-01E — delegated NPC fleet freight assignment, draft](engineering/SHIP01E_DELEGATED_FLEET_FREIGHT_20261105.md) — real commissioned carriers, canonical planetary work orders and physical deliveries; no pilot or resource authority transfer.

- [SHIP-01F — two-colony funded shipyard orders and real purchaser delivery, draft](engineering/SHIP01F_TWO_COLONY_SHIPYARD_CUSTOMER_20261105.md) — buyer/supplier approval, physical six-module payment, original twelve-tick yard, real hull handover and buyer NPC commissioning.

- [WORK-01A — universal NPC work coordinator and real customer shipyard fulfillment (draft)](engineering/WORK01_UNIVERSAL_NPC_FULFILLMENT_20261105.md) — typed native work adapters, durable consent/dependencies, v94 recovery, no duplicate inventory.
- [WORK-01B.1 — typed native work capability and prerequisite/NPC selection planner (draft)](engineering/WORK01B_CAPABILITY_PLANNING_20261105.md) — deterministic stage/age priority and per-tick real-worker collision prevention, original v94 resources unchanged.
- [WORK-01B.2 — canonical worker qualification and one-slip shipyard capacity (draft)](engineering/WORK01B2_NATIVE_WORKFORCE_CAPACITY_20261105.md) — native NPC duty, builder conflicts, physical slip occupancy, no new workforce or factory ledger.
- [WORK-01B.3 — native emergency policy, funding readiness and deterministic tie fairness (draft)](engineering/WORK01B3_POLICY_EMERGENCY_FAIRNESS_20261105.md) — exact colony safety signal, elective buyer funding/staffing holds, no phantom shipyard reservations, no synthetic rescue.
- [WORK-01C — original NPC shuttle and intercolony freight executors (draft)](engineering/WORK01C_NATIVE_LOCAL_AND_INTERCOLONY_EXECUTORS_20261105.md) — already posted native transport delegation, physical flight/manifest/arrival proof, actual recipient unload, v94 restart validation.
- [FLIGHT-01 — one generic autonomous NPC flight navigation service (draft)](engineering/FLIGHT01_UNIFIED_NPC_NAVIGATION_20261105.md) — mission-neutral real-ship NPC piloting, physical celestial-frame transit, original v72 travel receipts, v95 durable replay; future route adapters share this core.
- [FLIGHT-02 — native local approach and docking through the same NPC navigator (draft)](engineering/FLIGHT02_NATIVE_LOCAL_DOCKING_20261105.md) — original two-axis thrust/braking, native dock/undock commands and station-transition evidence, unified v96 flight records.
