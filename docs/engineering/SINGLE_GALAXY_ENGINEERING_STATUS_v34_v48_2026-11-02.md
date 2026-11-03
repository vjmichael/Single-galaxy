# Single-Galaxy — Engineering status and integration roadmap (v34–v48)
**Recorded:** 2026-11-02. **Project:** original Single-Galaxy simulation, not EVEmu.

## Scope and source of truth
The current native experiments are independently compiled/tested **focused C++17/SQLite source packages**, up to `DEMOG-05q6e5b5b v48`. The complete source has **not** been merged into the production/interactive game kernel, and native Windows/client acceptance has **not** been performed. GitHub is the source repository and engineering archive, not the build host. Individual immutable milestone reports, patches, test logs and SHA-256 manifests are in each named distributable package.

## Progress ledger
| Milestone | Verified behavior | Limit |
|---|---|---|
| v34–v39 | Ferry voyages, manifests, stranding/rescue, terminal casualty tombstones, clinic/medic availability, recoverable medical state | Native operating crew, family and child cases need separate reconciliation |
| v40–v40b | ART02 family + ECON03 regional cross-store checks; host casualty/family/region effects on one SQLite transaction | One file and stable ownership |
| v40c | Host-only birth records update ART02 child identity and regional population atomically | Dependent != individual playable worker |
| v41–v42 | Journaled adult home migration; complete registered guardian/dependent moves; physical child berths, boarding, in-flight custody, arrival | Only authorized host changes |
| v43–v44 | Paired parents, nonfatal family evacuation, region handoff fences; atomic two-parent legal household migration | Full dependent fatality protocol intentionally blocked |
| v45a–v45b2 | Read-only incident custody audits; pinned historical nonfatal stranding cases; verified rescue outcome and receiving-parent custody | Incomplete evidence never implies child death |
| v45b3 | Dual-parent authorized non-parent **legal** guardianship; region-scoped cold/detailed ownership-record fences | Host attests consent; warm industry/workforce simulation not yet reconciled |
| v46 | Non-parent **physical escort** transfer; atomic multi-family rescue to **one** receiving ferry | Separate crew and split destinations |
| v47a | Durable, non-executable two-receiver allocation intent | Recording intent does not move passengers |
| v48 | One canonical host command atomically evacuates manifested adults + ART02 dependents across **two** receiving ferries; independent receipt, restart/replay and crash rollback | **Native source-ferry operating crew not included** |

## Most recent verification evidence (v48)
Strict Release CTest **34/34 passed**, Debug UndefinedBehaviorSanitizer CTest **34/34 passed**, independent source rebuild CTest **34/34 passed**, complete archive SHA-256 manifest **252/252 matched**, and v47a→v48 patch verified. These are local focused-kernel test results, not full game acceptance.

## Persistent invariants
- One immutable NPC/ART02 child identity; at most one committed current physical custody, with separate immutable historical records.
- A committed world command, passport/manifest receipts, family record, regional checkpoint and any relevant society effect must agree; incomplete or forged links fail closed.
- Crash before COMMIT leaves every entity at its last committed custody. Never infer death from missing/corrupt rows.
- Child fatalities on ferries remain blocked; **never depict or visualize children being harmed or dying**. The safety policy is documented in `DEPENDENT_INCIDENT_NON_GRAPHIC_POLICY_2026-11-02.md`.
- Police/fire/full institutional EMS remain deferred at user direction.

## Next integration sequence and acceptance
**A. Source-ferry operating crew rescue:** extend the versioned split rescue operation to include a canonical, complete and disjoint native crew partition. Preserve each crew NPC ID, home, duty assignment, and custody; never silently leave native crew aboard after the vessel is declared evacuated. The two receiver population/crew counts, voyage records, world journal and child receipt remain one atomic transaction. Test recovery after failures between receiver writes, restarts, replay and adversarial tampering. Distinguish physical rescue from *permanent crew reassignment* and avoid invented staffing.
**B. Shared production-region ART02/ECON03 population:** reconcile registered family/dependent census, adult worker counts, physical presence, regional resident totals and inter-region custody under explicitly versioned owner/generation fences. No free residents or dual counting.
**C. Genuine cold/detailed (warm) promotion/demotion:** stage actual industry, worker, resource and population checkpoints, demonstrate deterministic catch-up and handoff CAS/rollback during live ferry transfers. A region owner-record toggle alone does not satisfy this gate.
**D. Full integration:** assess and merge tested source into canonical game, Linux and native Windows, installer/client and persistence acceptance; keep GitHub as repository.

## Related milestones
See `DEMOG05Q6E5B5B_ATOMIC_TWO_FERRY_EXECUTION_v48_2026-11-02.md`, `DEMOG05Q6E5B5A_SPLIT_RESCUE_INTENT_v47a_2026-11-02.md`, `DEMOG05Q6E5B4_PHYSICAL_GUARDIAN_MULTIFAMILY_v46_2026-11-02.md`, and previous reports in `docs/engineering`. This status is an index and does not supersede individual test logs or acceptance boundaries.
