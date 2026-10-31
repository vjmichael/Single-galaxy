# Single Galaxy — B7.0k Clinical Care Engineering Report

**Date:** 2026-10-31  
**Scope:** First-aid clinics, scarcity-triggered aggregate care episodes, real care inputs, stable local UI/relay contracts, SQLite v16 recovery.  
**Status:** Implemented and Linux-tested. The broader **renewable water/oxygen throughput and full healthcare** objectives remain open.

## Design intent and user-facing behavior

This milestone builds on the B7.0j community hall without treating a hospital icon as an instant happiness bonus. An authorized nearby colony ship constructs a basic clinic at an existing owned outpost **after housing is built**, paying two previously fabricated modules (4 tonnes). The ship must physically approach for construction and supply transfer.

A player may assign **0–4 medics** drawn exclusively from existing housed residents. Medics, community-hall attendants, and solar-utility technicians share the same finite resident pool; overassignment and staff-depleting transfer requests are rejected. A facility with no people does not record fictional missed care cycles. A staffed clinic adds **1 finite energy unit per simulation tick** to the site's solar demand.

Care supplies are currently a deliberately simplified **first-aid simulation**, not pharmaceuticals. Clean water and nutrient-based first-aid substrate are delivered in kilograms from the ship's genuine life-support inventory. When a staffed, powered clinic treats an aggregate shortage-related case, it consumes **1 kg of each**, leaving 1 kg wastewater and 1 kg persistent medical/organic waste in the clinic. Nothing is created or discarded for free. These supply streams are an *abstract care recipe*; they are not a medically valid substitute for medication or surgery.

## Case handling and service readiness

A baseline local food, water or oxygen shortage among residents or workers can generate **at most one new aggregate care case per occupied site per 20-tick period**, capped by the existing onsite headcount. Cases do not resolve merely by waiting or logging back in. A working medic can treat one existing case per needs cycle; treatment requires local stores and power. A reduced team can treat a case while still failing the stronger all-resident coverage requirement.

To evidence **basic care access**, a clinic must provide three successive powered, comfortable, fully staffed care periods, with spare water/substrate for additional treatments and no outstanding aggregate cases. One medic provides nominal coverage for four people. The resilience assessor checks **every occupied outpost** and reports supported clinic sites, open cases, treated cases and basic care access as a **separate CLINIC_ASSESS telemetry frame**. It does not silently change the older RESILIENCE field count or claim health/rest is fully modeled.

**Autonomous expansion stays disabled.** The prior `HealthRestUnmodeled`, `WaterRenewalUnverified` and `GrowthPolicyUnmodeled` blockers remain. Treatment history is not proof of individual medical outcomes, prescription medicines, sleep, long-term mental health, or actual autonomous institution management.

## Technical implementation

- Three authoritative new actions: `ConstructClinic`, `TransferClinicStock`, `AssignClinicStaff`. Input validation is enforced at submission, snapshot restore, the actual C++ executor and the browser-facing local relay.
- Canonical `ClinicFacility` record persists source ownership, embodied structure mass, water/substrate stocks, processed medical wastes, medics, aggregate cases, recorded and treated episodes, cycles, missed service and service streaks.
- **Snapshot v16**, preserving read compatibility for v1–v15 and not silently rewriting legacy worlds into v16 without a new action. Pending B7.0k commands restore with strict action/amount/selector checks.
- A second SQLite `resource_ledger` validates each successful clinic construction and supply transfer against independent command and execution journals. The ledger requires exact amount, direction, tick and authenticated source/destination semantics; missing or tampered receipts are rejected on recovery.
- Local C++ host emits `CLINIC` and `CLINIC_ASSESS` telemetry independently from the older `RESILIENCE` frame. JavaScript decoding bounds all fields, rejects contradictions and refuses unsupported growth certification.
- HTTP relay only admits the allowlisted `colonyClinicBuild`, `colonyClinicStaff`, and `colonyClinicStock` commands in colony mode, with strict Origin, session token, exact JSON shape, primitive types and numeric bounds. The browser does not assign user identity or issue privileged admin commands.
- Case counts are bound by local population: moving the last patients away without a care-transfer capability is rejected rather than creating a corrupted save. This is a deliberate temporary constraint until individual patient transfer is implemented.
- Old B7.0h–j code and UI behavior were preserved; clinics add power demand only when staffed.

## Verified test evidence

| Verification | Result |
|---|---|
| Native CTest regression suites | **47/47 passed** in 4 bounded groups: 1–12, 13–24, 25–36, 37–47 |
| JavaScript tests | **75/75 passed** |
| New B7.0k native/integration suites | **2/2 passed** |
| New B7.0k JS contract tests | **3/3 passed** |
| Linux production-style CMake configuration and compilation | **Passed**, `BUILD_TESTING=OFF`, `SG_QA_AUTOMATION=OFF` |
| Archive extraction, source checksum verification and rebuilt new native/live-relay tests | **PASS** — rebuilt both targets; **2/2 CTest** and **3/3 Node** checks |

The new C++ test covers construction prerequisites, existing-person staffing limits, finite stock and embodied mass, shortage-generated case, partial-coverage treatment, interruption, recent positive continuity, physical-distance rejection, overall mass conservation, snapshot v16 canonical roundtrip, independent SQLite recovery copy, unauthorized commands, invalid selectors, and tampered resource-ledger receipts. The Python live test checks C++ host telemetry, SQLite restart, served UI assets, accepted relay commands and rejection of malformed/forged requests. **Neither is a manual graphical Chromium click-through or native Windows test.**

A full regression had previously passed before a minor occupancy-guard change. An initial parallel full-suite rerun exceeded the command execution limit; it was *not* counted as evidence. The four bounded completed CTest groups above are the final evidence. Historical older test logs remain archived in the source package.

## Explicitly unimplemented / next priorities

- **Renewable water and oxygen throughput:** current farm and ship transforms conserve mass, and existing resilience checks intentionally refuse to certify water renewal from lifetime recovery totals. A next subgate must persist rolling **per-period** local recovery/consumption and oxygen generation/consumption history, reconcile it across all occupied sites, prove bounded spare capacity and distinguish imported stock from replenishment. No such proof was fabricated here.
- **Full medical systems:** individual NPC identities, diagnoses and treatment plans, real manufactured medical supplies beyond basic first-aid substrate, triage queues, infection dynamics, lasting morbidity, medical specialization, shift scheduling and patient relocation.
- **Governance and managed absence:** autonomous supply procurement, budget authorization, hospital/clinic delegated operations, safe offline catch-up and transparent return briefings, while preserving no inactivity punishment.
- **Platform validation:** native Windows game build and real Chromium click-through; full physically continuous 3D flight and private-server scale.

## Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -R 'galaxy_b70k_' --output-on-failure
node --test tests/*.js
```

Production:

```bash
cmake -S . -B build-production -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSG_QA_AUTOMATION=OFF
cmake --build build-production -j4
```

The `B70K_SOURCE_SHA256.txt` file accompanies the ZIP and checks the included source, UI and QA scripts against a clean extraction. Older `B*_SOURCE_SHA256` historical files document the corresponding previous milestone and should not be interpreted as hashes of the now-updated files.
