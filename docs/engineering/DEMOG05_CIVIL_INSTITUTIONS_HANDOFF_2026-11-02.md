# Single Galaxy — Civil institutions and transport/medical handoff (2026-11-02)

## Current verified scope

The passenger/EMS sequence is a set of **local focused C++17/SQLite kernel milestones**, not a merger into the full game build. This is the original Single-Galaxy simulator, not the EVEmu server.

| Milestone | Local result | Remaining boundary |
|---|---|---|
| DEMOG-05p / v34 | Passenger manifests, stable NPC custody, passenger capacity, cross-system voyages and independent replay | No incidents |
| DEMOG-05q1 / v35 | Stranding, repair/resume, nearby capacity-checked rescue | No ferry destruction |
| DEMOG-05q2 / v36 | Explicit all-fatality manifests, persistent wreck, immutable casualty records | No mixed survivals |
| DEMOG-05q3 / v37 | Atomic mixed survivor/fatality rescue, EMS transport report | Not definitive medical care |
| DEMOG-05q4 / v38 | Named medic training, built clinic and supplies, injuries and treatment, rehoming surviving ferry crew | No multi-episode or recovery clock |
| DEMOG-05q5 / v39 | Multiple injury episodes, deterministic recovery, finite mobile supplies, staff shifts, read-only population/workforce audit | Not yet connected to full-game family/workforce/cold-warm authority |

v39 local evidence: 15/15 strict Release CTest, 15/15 UBSan CTest, 15/15 independent clean extraction, 54/54 archive checksums. These numbers characterize the focused package only. The main repository documentation is not a code merge.

## Existing colony-life services in the full game

- B7.0j community hall: real constructed, staffed and provisioned service (quality-of-life proxy), not public safety.
- B7.0k clinic: actual first-aid structure, resident slots, power, materials, aggregate care; no hospital-wide medical simulation.
- Locally verified v38/v39 add named triage, rescue, training, treatment and recovery in a separate focused transport build.

## Explicitly deferred: full civil institutions

The user has deferred the integrated police, fire, ambulance/EMS dispatch and civil-institution framework until after DEMOG-05q6. No police precinct/patrol/crime investigation/arrest chain, firefighting engine or fire propagation/suppression, 911-like dispatch, service budgets, shared station deployment, or autonomous incident response is claimed implemented.

A later civil-services milestone should use existing actual NPC identities and qualifications; finite facility construction, machinery, vehicle and supply inventory; shifts; jurisdiction and policies; response range/time; conservation and journaled incident outcome; cold/warm fidelity; and unified dispatch with specialist police, fire and EMS roles. No new population ledger and no free response teleportation.

## DEMOG-05q6 underway

Goal: connect canonical v39 injury/availability events into verified *read-only* family/census and production staffing views, and cold/warm regional checkpoint reconciliation without creating another owner of people. Reconcile roster identities, home/physical location, deaths, dependent counts and revisions. A checkpoint must reject stale generation, duplicated people, mismatched counts or orphaned casualties, and must not advance cold-state mortality merely from wall-clock elapsed time.

Open caution: ART02 family births are tracked in a separate appearance/family store (with aggregate dependents, not immediately spawned NPC entities); a canonical cross-store transaction cannot be assumed. Region economics/ownership also use independent checkpoints and opt-in bridge with limited demotion. Make read-only projection explicit, report inconsistencies, and do not claim a full atomic integration until stores share a validated transaction boundary.

Source milestones remain separately delivered artifacts; GitHub is repository and milestone history, not development hub.