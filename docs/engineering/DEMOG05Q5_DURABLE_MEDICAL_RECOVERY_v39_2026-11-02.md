# DEMOG-05q5 — Durable medical recovery lifecycle v39

**Date:** 2026-11-02
**Project:** Original Single-Galaxy C++17/SQLite simulator (not EVEmu server).
**Baseline:** Local verified DEMOG-05q4 v38 source.
**Status:** Locally implemented and verified. NOT merged into the full game kernel, playable client, or region/family workforce system. GitHub remains repository/documentation, not the development hub.

## Implemented

- Multiple durable injury episodes per NPC: current MedicalCase plus retained medical_history keyed by (npc_id, triage_command_id). Past evidence is not overwritten on subsequent injuries.
- Deterministic recovery timeline: definitive treatment enters a recovering state and completes after 3 × severity logical simulation ticks. Old v38 treated cases retain legacy semantics; death never auto-recovers.
- Shift-qualified clinic treatment: host-authorized HostScheduleMedicShift records real named qualified medics and bounded staffing intervals; v39 treatment requires available on-duty staff. Concurrent shift reservations cannot exceed the clinic's physical staffing slots.
- Finite mobile supplies: HostLoadMedicalKit transfers real water and medical substrate from a built clinic to a ship, conserving world physical mass. HostMobileStabilize consumes those materials, writes an episode receipt and creates waste. Stabilization is not definitive cure.
- A trained named ship crew member can provide shipboard EMS in v39, with physical presence and clinic qualification; v38's prior qualification boundary stays compatible.
- Derived workforce and population APIs: npc_available_for_duty and assess_medical_population expose home-colony living/deceased, recovered/injured, in-transit, on-duty and unavailable counts using canonical NPC, ship, farm and habitat records only.
- Snapshot v39 adds history, schedules, kits, recovery due/complete fields; the existing transactional SQLite world journal and receipt authority are retained. Snapshot validators fail closed on forged historical cases, shifts, clinic totals, orphaned kits and contradictory receipts.

## Local verification evidence

- Strict C++17 Release build (GCC 14.2): **15/15 CTest passed**, including all 14 inherited v34-v38 regressions.
- Debug UndefinedBehaviorSanitizer: **15/15 passed**.
- Independent clean extraction from packaged v39 ZIP, fresh configure/build and test: **15/15 passed**.
- SHA-256 check of source/artifact bundle entries: **54/54 matched**.
- New medical-recovery regression constructs a real clinic, qualifies onsite and shipboard medics, handles a destroyed ferry and rescued crew, performs bounded shipboard stabilization and scheduled treatment, verifies multiple recovery episodes, shift expiry/refusal and renewal, no extra supplies consumed on failure, population accounting, durable restart and genesis replay, failed commit/retry, and corruption refusal.

## Limitations / integration gates

- Three-ticks-per-severity is a game rule, not a physiological recovery model; no injury progression, mortality modeling, surgery or disease diagnosis has been implemented.
- The source provides read-only workforce readiness, but the wider game factory/farm scheduling, ART02 family/kinship tables, faction rosters, and cold/warm regional population checkpoint system are NOT yet wired to it.
- Medical kit manufacture, salvage, container volume, return logistics and full EMS dispatch AI/UI remain separate features.
- Native Windows tests and the main game's full integration remain outstanding. This document is not a source-code merge.

**Local source deliverable:** SingleGalaxy_DEMOG05q5_Durable_Medical_Recovery_v39.zip with source, tests, v38-to-v39 patch, engineering report, CTest logs, and checksums.
**Next:** connect canonical medical availability and injury events to live family/workforce projections and regional cold/warm population reconciliation.