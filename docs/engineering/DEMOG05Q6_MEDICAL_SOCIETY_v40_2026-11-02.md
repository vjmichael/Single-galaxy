# DEMOG-05q6 — Medical society reconciliation and v40 opt-in workforce gate

**Date:** November 2, 2026  
**Scope:** Single-Galaxy original C++17/SQLite simulator, **not** EVEmu server  
**Baseline:** Local DEMOG-05q5 medical recovery v39, with real ART02b1 family and ECON03c10 regional sources  
**Result:** Locally verified focused v40 integration candidate. **Not merged into the full game kernel, Windows release, UI, or authoritative cold/warm owner.**

## Implementation

1. New read-only `reconcile_medical_society` binds the committed `PersistentWorld` snapshot and generation to the actual ART02 family registry and ECON03 regional population checkpoint in the **same SQLite file**. It independently checks named NPC/casualty totals, recorded family deaths, derived family dependents, clinic/medical unavailability, travel and assignments, and regional revision/population. It refuses stale world generation, missing casualty links, stale regional revision, detached source, forged family claims or a mismatched dependent count. The projection never creates NPCs or changes the family or cold-region source.
2. Added **genesis-only opt-in `Simulation::enable_medical_workforce_rules()`** and a `galaxy-kernel-v40` snapshot marker. Existing v34–v39 snapshots retain their former interpretation. In opted-in new worlds, farm/habitat assignment selects only physically present, medically fit NPCs; insufficient ready staff yields `NoResources`. Farm crop cycles, XP and workshop operators now use actual available worker identity rather than only static job assignment counts. Sick workers still consume physical life-support resources.
3. Actual fixture: 20 founding residents, 4 ferry crew and 8 EMS crew; family founder identities, one ART02 birth held as a non-workforce census dependent, ferry rescue with one home casualty, explicit family death receipt, real regional population update, injured passengers, disembarkation, physically constructed and provisioned farm. **Injured Bob is not assigned to farm labor**; a healthy replacement works and gains experience. Same-world regional population and family child counts continue to reconcile.
4. Fault cases: before family death update or regional population revision, certification refuses; stale world generation and stale checkpoint revision are refused; a family-only death without a matching canonical casualty is refused. Durable restart and independent world-journal replay revalidate the family and regional state.

## Local verification

- GCC C++17 Release, strict warnings: **16/16 CTest passed**.
- GCC Debug with UndefinedBehaviorSanitizer: **16/16 CTest passed**.
- Repackaged v40 ZIP, fresh independent extraction and Release build: **16/16 CTest passed**.
- File integrity inside final ZIP: **76/76 checksums matched**.
- Archive: `SingleGalaxy_DEMOG05q6_Medical_Society_v40.zip`, 288092 bytes, SHA-256 `e31306340b58b345ad741c282b1108981cee12f8c1bd564a0aa4e0db352ee3b1`. Full source, v39-to-v40 patch, test logs and engineering report included.

## Explicit outstanding integration gates

- **No automatic multi-store write transaction.** The trusted host must independently record family deaths/births and scoped regional population changes. Reconciliation detects missing updates and refuses acceptance. An authoritative single-transaction write path or durable staged journal is needed before calling family+region migration fully integrated.
- **No full ECON03 owner handoff.** The checkpoint is genuinely persisted and verified but its `RegionalModeAuthority` ownership generation/phase, live factory pipeline and cold/warm handoff have not been made atomic with the ferry and medical journal.
- Old v39 saves do not silently receive the new v40 labor rules. A journaled migration remains to be built. The wider payroll, family caregiving, work rota, large-scale colony simulation, client/UI and native Windows tests are open.
- Full **police, fire, public safety dispatch and full institution-level EMS** are explicitly deferred by the user until this integration track progresses. The existing first-aid clinic and ferry transport do not constitute complete civil institutions.
- This repository commit is documentation only; **do not claim that the focused source bundle is merged into GitHub main**.

## Next

DEMOG-05q6 continuation: a trusted atomic world/family/regional custody writer with idempotent receipts and verified owner-generation fences, followed by multi-colony cold/warm evidence and full family/workforce scheduler integration. Separate civil-institutions milestone comes afterward.

The consolidated earlier EMS and deferred-institutions handoff is in `docs/engineering/DEMOG05_CIVIL_INSTITUTIONS_HANDOFF_2026-11-02.md`.