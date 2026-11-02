# DEMOG-05q4 — Real EMS Medical Care v38

**Date:** 2026-11-02  
**Scope:** Original Single-Galaxy C++17 / SQLite focused world kernel, based on the v37 mixed-EMS bundle.  
**Status:** **Implemented and locally tested**. **Not merged into main game kernel, client, or cold/warm regional workforce integration.** GitHub is an engineering repository, not the development environment.

## Implemented

- Added three new host-authorized, journaled actions: `HostTrainMedic`, `HostTriageRescued`, and `HostTreatInjuries`. The existing canonical `PersistentWorld` remains the sole authority. `submit_host_medical_action` is a dedicated host interface using the same world transaction.
- Existing physically constructed clinics, habitat residents, assigned clinic staff slots, local water and medical substrate now support individually recorded EMS care. A named qualified medic must be an actual NPC present at the clinic, with >=3 audited training units that consume 1 kg of water and 1 kg of substrate per unit. No spontaneous medic generation.
- Injured survivors have durable severity 1–3 and triage/provenance records; treatment has an immutable command/tick/medic/clinic receipt. The status projection distinguishes healthy, injured, treated, deceased, unknown. Treatment is not yet full biological healing.
- EMS treatment consumes 1 kg water and 1 kg substrate per injury-severity unit, emitting corresponding local wastewater and medical waste; per-tick treatment cannot exceed physically assigned medic slots and the number of qualified named medics.
- Mixed ferry loss can now rescue and **rehome native ferry crew** without erasing their identities. Crew changes are conserved across the wreck and receiving ship, and an immutable `CrewRehome` provenance record links each living crew member to the original ferry loss.
- `galaxy-kernel-v38` maintains old v34–v37 action-number and snapshot compatibility and adds explicit medical and crew-rehome records. Snapshot validators cross-check named NPCs, patient treatment totals, medic training inputs, crew and voyage lineage, casualty counts, receipt existence, and clinical stock conservation.

## Verification (Linux)

- GCC Release: **14/14 native CTest tests passed**.
- GCC Debug + UndefinedBehaviorSanitizer: **14/14 passed**.
- Clean extraction from packaged source ZIP, independent Release build: **14/14 passed**.
- The new `demog05q4_medical_care` regression exercises a real ore-to-construction clinic, two named trained medics, mixed ferry loss with surviving native crew, one casualty from a remote home ship, three triaged patients, two-slot treatment capacity, delayed third treatment, material consumption, SQL failure/retry, unexecuted treatment restart, saved injury restart, independent genesis replay, and corrupted medical/crew receipt refusal.
- All 13 inherited v34-v37 tests remain green.

## Limits and next work

This local source package is **not** a full-game integration and is **not** native-Windows verified. Medical outcomes and casualties are authorized host inputs; no deterministic physical damage or disease progression is modeled. Medical treatments do not yet schedule recovery time or staff shifts. General genealogy, payroll, mobile medkits, faction health services, multi-region cold/warm population projections, and the game client remain separate integration gates. A second injury to the same NPC needs an episode-history schema. Avoid inventing mortality from a database restart.

**Next gate:** DEMOG-05q5 — episode-based medical history, treatment/recovery timeline, mobile medical inventory, shift-qualified staffing and the authoritative broader family/workforce/macro-population integration.

**Deliverable:** Local `SingleGalaxy_DEMOG05q4_Real_EMS_v38.zip` (focused source, test evidence, engineering report and patch); do not represent this doc-only GitHub commit as a code merge.