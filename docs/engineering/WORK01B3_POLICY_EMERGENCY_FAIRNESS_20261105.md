# WORK-01B.3 — Native Safety Policy, Funding Eligibility and Fair Scheduling

**Status:** Source and test implementation committed as a draft stacked above WORK-01B.2. Exact-head Windows/Linux CTest and original v94 crash/recovery acceptance remain unverified until GitHub Actions completes. No new persisted snapshot format, workforce/stock ledger, emergency executor, worker authority, or customer/supplier consent is created.

## Original-native policy signals, not speculative rescue

`Simulation::process_universal_work` now performs one read-only `colony_safety::assess` at the start of each canonical world tick for each actively delegated buyer's onboard colony compartment and any occupied farm/habitat compartments belonging to that buyer. It checks the existing safety model's *Critical* or *Emergency* `emergency_override`, which derives from real finite food/water/oxygen stocks, actual occupants and committed shortage state. The coordinator does not fabricate a second emergency classifier, move resources or people, claim to have performed repairs, or supersede the native emergency supervisor. No stale browser signal or unverified user hint can grant an emergency flag.

**Preemption scope:** Verified critical life-support risk temporarily pauses the buyer's **elective six-module funding** and optional **two-NPC commissioning transfer**. It does **not** cancel approved contracts, claw back payments, interrupt the supplier's original twelve committed build steps, reverse a launched hull, prevent already-funded uncrewed hull handover, or block an existing emergency rescue/supply system. The buyer's critical compartment does not give that buyer authority over a supplier's NPCs; the supplier still must accept independently.

The pause is recomputed on each native tick and automatically lifts when the canonical compartment is no longer Critical/Emergency. This is a protective *hold*, not a positive rescue/repair action. An emergency-purpose carrier or explicit override would need its own future narrowly-scoped, owner-approved rescue authority; this phase does not guess which civilian ship orders are survival-essential.

## Eligibility before scarce-capacity selection

`Simulation::can_fund_shipyard_customer_order` is a read-only native preflight over **the same original world objects**. It confirms customer ownership and supplier acceptance, the purchaser's six actual manufactured modules, valid same-frame physical berth and stationary ships, supplier ownership of an operational yard, available original builder NPC duties, free genuine assembly slip, supplier life-support minimums, frame validity, and no conflicting physical transit/build. It never edits the source contract or spends inventory. A failed preflight becomes `Gate::FundingNotReady`, which claims **no** per-tick yard/worker slot. Other authorized, physically ready work can proceed.

This is an **eligibility preview, not a transaction**. The original `fund_shipyard_customer_order` and `queue_shipyard_freighter` remain the sole effect authorities, and repeat every final native check inside the original world transaction. An intervening world mutation may still make funding fail. WORK-01A's real two-owner physical test now asserts that the preview is true before the successful six-module payment, false for the foreign owner, false after paid completion, and never changes canonical snapshot bytes.

## Priority bands and starvation-resistant fairness

The deterministic policy classes are `NewDiscretionaryFunding`, `CommittedConstructionOrHandover`, and `FinalStaffing`. The existing planner's **physical stage** has absolute priority (Delivered, Funded, Accepted, Requested), then bounded tick age up to 999. No dynamic UI or local/browser score overrules original source receipts or authorization.

Within exactly equal priority bands (including different old posting dates after the age bound is reached), eligible candidates rotate by `tick / fairness_quantum_ticks` modulo the number of candidates. The default quantum is one **committed simulation tick**. The tie cohort is sorted by original directive ID before rotation. No random seed, wall-clock, mutable shadow queue or indefinite NPC escrow is introduced.

A blocked/unfundable candidate remains observable, but is **not permitted to reserve** a scarce physical worker or slip, preventing a merely old but unexecutable request from repeatedly hiding a ready peer. Cancelling a directive releases its derived slot selection, not any real already-paid material. This is *bounded tie fairness*, not a proof of fairness for continuously different physical stages, non-cooperating suppliers, or broader task categories that have no native delegation executor yet.

The policy produces temporary `Gate::EmergencyHold` and `Gate::FundingNotReady`, mapped to the existing v94 `WorkBlock::PhysicalFunding` or `NamedCrewAndSupplies` receipt types. No unauthorized schema migration is assumed. Owner-visible differentiated policy reasons remain a UI and persistence follow-up.

## Acceptance tests

- `tests/WORK01B3_EmergencyFairnessTests.cpp`: two scoped buyers contest a single original installed slip; an older buyer that cannot physically pay gets no reservation; a funded buyer proceeds. A critical-risk buyer pauses elective work while the other can use the same supplier's free slot. Existing funded hull completion/handover continues. Crew staffing waits and recovers after safety clears. Two requests aged above 999 ticks alternate tie precedence across committed ticks; same-tick replay is byte-for-byte deterministic in selection identity and gates. Cancel releases the transient slot.
- The same test calls real `colony_safety::assess` on physical stocked and critically stocked native colony ships (200 real crew versus 200 kg of each needs resource), ensuring severity is produced by the existing safety engine, not the planner.
- `tests/WORK01A_UniversalFulfillmentTests.cpp`: native two-owner accepted customer contract, actual modules/supplier/builders, read-only funding preview and subsequent original physical debit, launch, commissioning and SQLite rollback/restart.
- `.github/workflows/work01b3-emergency-fairness.yml`: Windows/Linux native C++/SQLite builds and CTests for WORK-01B.3, WORK-01B.2, WORK-01B.1, WORK-01A, SHIP-01F, SHIP-01B.2, SHIP-01A and COLONY-AUTO-04A.

## Still not claimed

- Player-editable, durable per-work-priority rules and emergency exceptions are **not implemented**. The policy is a conservative built-in native rule, not an owner preferences UI.
- No automatic mining, agriculture, freight, factory crafting, repair or emergency resource-hauling executor is added by WORK-01B.3. The only delegated action remains a specific buyer-approved customer ship contract.
- Critical emergencies can block discretionary commissioning even where the buyer wanted a supply ship; an explicit survival-use policy must be engineered with original authorization/transport rather than guessed.
- Cross-task and per-owner weighted fair queuing, supplier market fairness, starvation across permanently different physical stages, and a guarantee of wall-clock completion are outside this gate.
- No broad Windows installed gameplay, multi-owner browser account session or native CI green status is asserted.

**Next gate:** WORK-01C can add adapters for existing local deliveries and actual intercolony freight using the original native transport manifests and work-order authority. Do not fork a new transport or inventory system.
