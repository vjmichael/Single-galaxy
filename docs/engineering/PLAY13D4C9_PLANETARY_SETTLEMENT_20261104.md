# PLAY-13D.4C.9 — Permanent Planetary Settlement and Workforce Transfer

**Status 2026-11-04:** 4C.9A admission preflight and native CTest source committed. The authoritative population writer, persistent migration receipts, migration policy integration, GUI commands, full crash-injection matrix and exact-head native acceptance remain **open**. Do not infer settlement is functional from this gate's pure policy test.

## Current authority

4C.8 leaves surface visitors in `PlanetaryShuttleState::disembarked_ids`, with optional temporary `duty_ids`; the named `NpcRecord` still has a parent-ship permanent assignment. Shuttle flight and custody are real, but there is **no permanent surface residency mutation**. The existing `SocietyMigrationPlan`, `HostSettleMigrant`, voluntary immigration admission, physical population ledgers and family-custody checks must be reused instead of inventing a new NPC roster.

## 4C.9A — Read-only eligibility, implemented

`include/galaxy/PlanetarySettlementAdmission.hpp` defines deterministic, fail-closed `preflight_settlement_admission`, which is entirely pure and can be called by native desktop or host code once facts have been grounded in the canonical world. It rejects invalid principals/site, absent or in-flight visitors, repeated transfers, unresolved household custody, missing voluntary consent, competing assignments, and missing *finite* housing, work slots or life support. `tests/PLAY13D_PlanetarySettlementAdmissionTests.cpp` exercises the reason codes; CMake registers its native CTest.

**Security:** never accept client-provided boolean admission facts as trusted input. They must be computed under the authoritative world lock using actual site/ship ownership, physical visitor custody, individual and dependent records, immigration policy, and inventory reservations. A successful pure check is **not** an admission or an allocation.

## 4C.9B — Authoritative commit (OPEN)

Implement one host-owned `PersistentWorld` operation, e.g. `settle_planetary_visitor(owner,ship,key,npc,request_id,admission_receipt,FailPoint)`:

1. Require genuine named NPC, the exact PGF surface key and site owner, commissioned port, and physical presence in `disembarked_ids`. Reject shuttle/ferry flight and elsewhere-assigned personnel.
2. Obtain the existing voluntary immigration/settlement consent proof and family-custody disposition from the real society/migration subsystem. A silent role change is forbidden.
3. Reserve finite settlement housing, support stock and permanent work capacity; do not treat the site's existing two crew slots as unlimited. Support departure/repatriation and dependency after the transfer.
4. Make one candidate world. Apply the preflight on facts extracted **from that candidate**; consume the existing migration/settlement event exactly once; remove the NPC from disembarked and temporary duty ledgers; transfer permanent residency/assignment to the same surface region; update canonical ship and site headcounts/roles consistently. Never create another `NpcRecord`.
5. Write durable unique operation receipt and snapshot in the same SQLite transaction, then publish on commit. Rollback must leave population, occupancy, family custody, inventories and shuttle manifest unchanged.
6. Validate reload: one person, one custody, one residence, one duty; no lingering ferry claim, no orphaned dependents; replayed request returns the committed receipt without repeating migration.
7. Distinguish **resident** from **employed**. A consenting resident may have no job; full work slots must block an employment assignment, not force deportation or silently fabricate work. Future multi-role/colony hiring belongs to a distinct explicit job assignment operation.

Before implementing step 3, extend existing planetary facility capacity/life-support representation as real persistent resources and decide how resident supply differs from a temporary visitor support cadence. No unconditional `permanent_work_slot_reserved` placeholder may be used in production.

## 4C.9C — Household and rescue integration (OPEN)

Reuse the established guardian/dependent co-manifest and migration acceptance flow, not a new shuttle-specific child registry. Initially refuse permanent family immigration unless the entire co-dependent movement can be committed atomically, with all required people physically present, legal guardianship reconciled and enough seats/housing/support. Do not strand a child while transferring a guardian. A failed site admission leaves every person in their original physical custody. Emergency evacuation and settlement reversals must use explicit transport, not teleportation.

## 4C.9D — Native commands and gameplay (OPEN)

Add explicit `SETTLE` (admission) and `ASSIGN_PERMANENT_WORK` (employment) to the native desktop command service only after 4C.9B is complete. Relay/browser remains the temporary test adapter. Report residency, job assignment, household admission, per-person finite support and receipt; show resident vs visitor separately. Orbital player never walks on the planet.

## Acceptance gates

- Native tests: forbidden site/owner, no consent, occupied ferry, absent visitor, full site, no support, competing jobs, already-settled replay.
- Real migration: same NPC ID before/after; sum of physical population unchanged; source crew decremented once; site resident capacity incremented once; no duplicate/shadow workforce.
- Family matrix: guardian with dependent, nonparent guardian, two guardians, mixed household, interrupted co-manifest; require no child left behind.
- Crash matrix: fail after receipt write, snapshot write, prior to commit; independent process restart, no ghost settlers.
- Physical operations: resident remains after shuttle ascent; subsequent ship departure does not consume their food; settlement consumes finite planetary supplies; without support work pauses or lawful evacuation is triggered.
- Security: owner, idempotency, exact region revision, malformed native/relay input, stale confirmation, expired admission.
- Exact-head Windows and Linux native CTest + real host/relay E2E + independent SQLite custody and inventory audit.

**Gate close rule:** 4C.9A alone is not a completed transfer. Keep 4C.9 and 4C.8 native acceptance unverified until their exact-head build/test results pass.

## 4C.9B checkpoint — candidate transition invariants committed (2026-11-04)

`include/galaxy/PlanetarySettlementTransition.hpp` introduces a strictly read-only **postcondition** guard, `valid_settlement_transition`. The proposed single adult transfer must retain the same named identity, keep physical position on the exact surface, reduce parent ship crew by one, increase permanent surface workers by one, remove shuttle visitor custody and temporary duty, preserve global population and physical material mass, and require independently verified consent and resource/household reservations. Unresolved guardianship and active transport fail closed. `tests/PLAY13D_PlanetarySettlementTransitionTests.cpp` covers valid and invalid projections; CMake registers the test.

**Not yet implemented:** no calling code currently obtains authenticated consent or real finite housing reservations, invokes this guard on a candidate `Simulation`, changes `NpcRecord` roles, updates `PlanetarySiteFacility` workforce capacity, publishes a SQLite migration receipt or exposes a `SETTLE` action. The existing colony migration writer requires ferry transportation and colony-ship destinations. It must **not** be tricked with invented ferry arrivals or colony ship identities to process a planetary shuttle visitor. We need a new settlement adapter over the existing authoritative family/census transaction with real site-level capacity and versioned persistence; until then, these checks are safety prerequisites, not a working settlement feature.

The current postcondition guard only models **employment-bound single-adult** moves. Separate non-working permanent residency and atomic dependent co-moves remain required before treating the broader 4C.9 gate as complete.

## Shared transport refactor checkpoint (2026-11-04)

**Direction confirmed:** immigration is destination-independent and transport-mode-independent. The historical `HostSettleMigrant` implementation and `step_with_atomic_migration` still assume completed ferry voyages and colony-ship destinations. Those assumptions are technical debt, not intentional game mechanics. Do NOT introduce an independent planetary immigration system.

**Committed implementation:** `include/galaxy/TransportIdentity.hpp` centralizes sorted/unique named manifests and evidence that an individual reached the requested origin/destination. `Simulation.cpp` now calls shared `transport::completed_arrival` during both host-migration admission and application; shuttle visitor duty and reboarding use shared `transport::completed_shuttle_visit`. Both operate on the same `NpcRecord` identity and do not invent a ferry. New `PLAY13D_SharedTransportIdentityTests.cpp` checks ferry and shuttle modes and rejects duplicate identities, wrong endpoints, missing transport receipts, in-flight states and absent passengers. CTest is registered.

**Remaining unification, not yet delivered:** replace the ferry-only prerequisite in `step_with_atomic_migration` and related family/region store validations with a canonical multi-modal transport receipt; introduce a typed settlement endpoint representing an orbital colony or exact PGF surface site; reuse one population/consent/family/census writer for both. Ferry and shuttle should have vehicle-specific flight state only. The same writer must guard permanent residency, housing, dependent custody, work assignment and crash-safe SQLite publication. Version existing receipts without rewriting old saves, and prove both old ferry migration and new planetary settlement in native Linux/Windows and independent DB restart tests.

Until these are complete, **4C.9B is partially implemented only**. The new shared transport code is not proof that permanent planetary immigration works.

## Universal NPC transport endpoints — refactor checkpoint

The old word **ferry** now refers only to a legacy *vehicle adapter* and existing save/command API; it must never imply that origin/destination are colony ships. The shared transport contract in `TransportIdentity.hpp` now models explicitly typed endpoints (`ColonyShip`, `SpaceColony`, `PlanetarySite`, `OtherNpcLocation`) and vehicle modes (`Ferry`, `PlanetaryShuttle`, `SurfaceVehicle`, `OtherVehicle`). One `completed_arrival` check handles named manifest identities, durable arrival receipts, endpoints and arrival state for all these modes.

The existing `Simulation` ferry migration checks and planetary shuttle visitor-duty/reboarding checks now invoke that common validation function. The latter uses its genuine committed shuttle descent receipt and physical disembarkation roster; no fake ferry is recorded. Cross-mode tests cover arbitrary location pairs and reject bad manifests, pending trips and endpoint mismatches.

**Compatibility and security boundaries:** This is currently a transport *verification unification*, not a complete runtime movement refactor. Legacy `VoyageRecord` continues to store ferry-specific physical flight state, and `HostSettleMigrant`/society admission still enforce old colony-ship endpoints. Those restrictions must be lifted in the authoritative migration/census writer only alongside a typed persisted receipt, genuine family/consent proof, capacity and host-owned SQLite atomic publication. Do not label planetary or arbitrary-location permanent resettlement operational yet, and do not accept transport evidence supplied directly by clients.

## NPC transport class unification checkpoint

`include/galaxy/NpcTransport.hpp` now defines the common **NpcTransport** domain model. An NPC transport is a vehicle-carried journey between any two typed endpoints, with one durable transport receipt, canonical named passengers, seat capacity and journey state. Ferry, planetary shuttle and surface vehicle are modes of the same operation; they are not separate immigration systems. `Simulation.cpp` now uses `completed_transport(NpcTransport)` in both the existing ferry settlement preconditions and planetary shuttle visitor-duty/reboarding rules. The shared transport CTest covers the same class against all three modes, duplicate passengers, capacity overflow, missing receipt and incomplete journey.

The class is an immutable **projection from authoritative vehicle records**. Legacy `VoyageRecord` and `PlanetaryShuttleState` remain the actual persisted host state and are not replaced by a second competing manifest. Rebuilding one typed transport from committed records is safe; accepting one constructed by the client as authoritative evidence is forbidden. Vehicle operation, fuel, landing and navigation remain vehicle-specific.

**Still open:** generalized migration writer and typed persistence for migration from arbitrary NPC locations, family/dependent custody over shuttles, capacity reservations for permanent nonworking residents, and exact-head native Windows/Linux acceptance. Do not claim all travel uses the new class as its sole writer yet.

## Canonical arrival lookup checkpoint — host state only

`Simulation::npc_transport_arrival(receipt,npc_id)` now projects a shared `NpcTransport` from actual completed ferry voyages or a shuttle's physically disembarked roster and recorded descent. `Simulation` now uses this host-derived proof in both legacy `HostSettleMigrant` checks and shuttle visitor duty/reboarding; clients cannot forge an authoritative `NpcTransport` merely by providing a syntactically valid manifest. Existing ferry-specific destination and family checks deliberately remain active until the universal migration writer is ready.

A native regional-facilities scenario was repaired: its second shuttle descent/reboarding block had been accidentally placed before the passenger was declared and before the first journey. The block now follows the first ascent and verifies an NPC's current surface presence against the actual arrival lookup.

**Important limitation discovered:** the shuttle currently stores only a site-level *latest descent receipt*, not an immutable per-passenger disembarkation receipt. A visitor staying on the planet across subsequent shuttle trips can be present in the current visitor roster even though the latest descent receipt belongs to a different passenger journey. This is adequate for the current physical-presence/return-pickup checks, but **not** sufficient legal/administrative migration proof. Before accepting a shuttle arrival into `step_with_atomic_migration`, introduce durable per-person arrival receipts (plus a historical transport journal) and bind the actual individual, source, destination, transport and completion generation. Do not treat the latest site descent ID as that person's original immigration receipt.

Native Windows/Linux workflow acceptance is still required; repository commits alone do not constitute execution.

## PLAY-13D.4C.9B2 — Individual arrival custody and ship control roles (2026-11-04)

**Committed:** `ShipControl.hpp` introduces `UserShip` and `NpcShip` as control-policy views over the **same physical ship entity**. They do not allocate duplicate ship hulls, inventories, pilot physics, or alternate permission stores. The control check is a policy primitive only; it does not replace host authentication. The corresponding native CTest exercises user vs delegated NPC permissions.

**Committed:** v85 shuttle snapshots persist a sorted `visitor_arrival_receipts` map keyed by each disembarked named NPC, to the actual descent receipt used when that individual arrived. The immutable flight request registry already retains that receipt. Another shuttle descent does **not** overwrite the person's arrival identity. Reboarding clears *active visitor custody* and its per-person receipt; old completed flight IDs remain immutable in the world registry. `Simulation::npc_transport_arrival` can now resolve an individual v85 landing without confusing it with the shuttle's latest descent. Native regional-facilities regression checks repeat descent while a visitor stays at the site, duty toggling, reboarding and snapshot round-trips.

**Backward compatibility:** old v84 visitors have no per-person arrival receipt. They may still physically reboard with existing authorization and custody checks, but must **not** receive permanent immigration approval from a guessed or synthesized personal journey. A new verifiable trip is needed. No fabricated ferry voyage is introduced. The next universal migration writer must bind consent, household travel, arrival and capacity in one authenticated SQLite commit rather than rely on the pure `NpcTransport` view.

**Verification:** `.github/workflows/play13d.yml` now compiles and runs the shared transport, ship-control, settlement preflight and transition tests on Windows/Linux as well as existing regional and native shuttle host E2E. Exact-head CI must finish green before these changes are considered accepted.

**Not yet completed:** the full shared migration/census writer is still ferry-specific, ship-controller roles are not yet wired to the live vehicle command broker, and v85 records a currently disembarked visitor's original flight receipt, not a universal historical per-person trip journal covering every vehicle/previous visit. Do not claim permanent planetary residency is operational.

## Universal NPC Migration Writer — shared preflight integrated (2026-11-04)

**Implemented and committed:** The preexisting `PersistentWorld::step_with_atomic_migration` now resolves both the primary traveler and any co-parent using `Simulation::npc_transport_arrival`. It validates the actual host-owned `NpcTransport` receipt, mode and endpoints before the existing family/census SQLite transaction. Direct raw `VoyageRecord` passenger checks are removed from these two writer preflights. No second SQLite migration writer or passenger ledger has been created.

A new `UniversalNpcMigration.hpp` provides the **single transport-neutral admission gate**, with typed endpoints, physical custody, household reconciliation, consent, housing and life-support reservation requirements, world-generation CAS and competing-residence checks. The existing atomic writer now calls this gate after verifying family revisions and voluntary admission. C++17 `Endpoint::operator!=` is explicit. Native CTest cases cover colony, planetary and surface movement modes, missing consent, family custody, stale generations, housing/support shortages and old ship migration behavior. The PLAY-13D Linux/Windows workflow builds/runs this new test.

**Compatibility:** Historic colony-to-colony migrations retain their established crew-capacity checks and do not invent a retroactive housing/life-support reservation. New planetary settlements MUST provide real reservations; the shared gate defaults to requiring both. Temporary visitation never causes an implicit immigration.

**Critical outstanding implementation:** `HostSettleMigrant` still mutates only ship-colony endpoints; `SocietyMigrationPlan` and its SQLite family/region stores do not yet encode a physically built planetary residence/available housing inventory, and dependent ferry travel verification is not yet generalized for shuttles. Accordingly the writer *validates* any host-derived `NpcTransport` contract at its common boundary, but only the existing ship-to-ship branch can currently complete the atomic population move. To enable a planetary commit without duplicate logic: (1) construct persistent on-site housing and finite support reservations; (2) extend the existing migration plan/host command to typed endpoints; (3) apply site resident and source headcount mutations to the same candidate world; (4) write one existing society migration/household/regional receipt and world snapshot transaction; (5) verify failpoint rollback, dependent co-move, replay/restart and authoritative census invariants. Do NOT implement a second planetary-only migration writer or bypass family history.

**Verification pending:** exact-head native Windows/Linux compilation and regression results.

## Typed settlement endpoint follow-on and census blocker (2026-11-04)

`SocietyMigrationPlan` now carries `origin_kind` and `destination_kind` (default `ColonyShip` for existing aggregate initializers and legacy plans). `PersistentWorld::step_with_atomic_migration` binds those typed endpoints to its host-resolved transport evidence and explicitly rejects non-ship endpoints until the family/regional residence accounting is upgraded. It is safer to reject than to identify a planet's surface custody ID as an existing colony ship.

**Verified in source inspection:** `reconcile_medical_society` currently requires `sim.colony_ship(home_ship)`, uses `MedicalAudit::by_home_ship`, and checks family and regional census populations under that same ship ID. Yet planetary delegated workers already have a different physical `location` and `assigned_site` while retaining their origin `home_ship`. Therefore **legal residence cannot be equated with either physical presence or job assignment**. The durable migration design needs an explicit residence identity per NPC and a reconciled planetary population/census facility, with compatibility defaults for old ships. It must not decrement an origin census twice or count visitors and permanent workers as new births.

Before enabling actual planetary commits, implement (1) explicit legal residence for named NPCs and dependent cohorts; (2) a source-of-truth settlement-capacity and life-support reservation stored with built facilities; (3) region/family/medical census reconciliation for typed endpoints, including old ship-ledger compatibility; and (4) atomic settlement mutation and family event publication through the existing `Store::commit` callback. Retain co-parent and dependent physical co-manifest proof. No second migration writer.

## Universal settlement census — v86 read-side implementation (2026-11-04)

A single canonical `NpcRecord` now exposes `legal_residence()` independently of its physical `location`, workforce `assigned_site`, and historical `home_ship` logistic authority. Version **v86** snapshots append the residence kind and ID to each named live NPC and casualty tombstone. Legacy v85 snapshots remain readable and stable: missing residence data projects to the historical home ship. `upgrade_legal_residence_snapshot_format()` stages an explicit in-memory v85→v86 conversion; it is not an implicit mutation of a committed SQLite world and production callers must journal any promotion.

`Simulation::assess_npc_residence()` recomputes both typed legal-population counts and physical occupancy from the existing `npcs_` map. It allocates **no second population ledger**. Snapshot loading rejects unknown or unregistered legal settlement IDs and cannot infer a legitimate home from a current transport location. The existing ship departure, ship-to-ship settlement, and rescue rehoming paths preserve the now-explicit ship residence identity. The native regional-facilities test checks v86 round-trip, distinct planet presence vs ship residence, total identity conservation, and rejection of a forged unregistered address.

`reconcile_medical_society()` now reads that canonical legal residence projection, recognizes actual colony ships, built outposts with habitats, and commissioned planetary sites, and separately accounts for the fourth (`PlanetarySurface`) worker role. The original family revision, population and regional checkpoint checks remain compulsory. A location merely existing does **not** establish population, give consent, reserve housing, or register an immigration permission. Older colony population still resolves identically by default.

**Still open:** the actual `HostSettleMigrant` mutation and host `SocietyMigrationPlan` transaction remain restricted to a colony-ship destination. Planetary legal residence cannot yet be admitted by gameplay because there is no durable finite housing capacity/reservation, no typed planetary family/region colony registration and no shuttle-dependent co-migration writer. The v86 support is preparation for that *same* SQLite writer rather than a working separate settlement pathway. Do not merge this gate as complete until native exact-head acceptance and atomic settlement/housing/family E2E succeed.

## Player-directed infrastructure rule — binding planetary gameplay decision (2026-11-04)

**The player controls what infrastructure is built, everywhere.** A planetary survey or resource shortfall can reveal an opportunity, but must not spontaneously create a mine, farm, power generator, processing facility, habitation block, road, spaceport expansion, or transport network. The player may either place/build directly through the permitted game UI or issue an explicit construction directive to NPC workers. **NPCs perform construction and routine operations; they cannot invent player-owned infrastructure without a player-authorized plan.** This also applies to the first spaceport: player chooses its site, supplies finite construction resources, and delegates NPC crews to assemble it over time.

When a colony lacks a required resource, the intended gameplay is **discover → survey → player authorizes facility/blueprint → gather/haul finite materials → NPC construction → maintain and operate → accumulate output → player arranges recovery/logistics**. Do not add a commuting-home requirement; resident NPCs may remain and work on the planet. An automated atmospheric approach/landing at an existing built spaceport is a future convenience, **not** permission to create or upgrade a port. Player-directed *what/where*, NPC-delegated *how*, and finite inventory/construction time are separate requirements.

For any future autonomy/management feature, NPC managers may recommend projects or carry out specifically authorized standing construction programs, but cannot silently change the player's infrastructure plan. Avoid using this rule as justification for new permit, immigration, paperwork, or relationship subsystems; prioritize the basic playable resource loop.
