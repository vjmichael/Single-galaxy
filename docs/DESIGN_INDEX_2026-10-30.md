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
