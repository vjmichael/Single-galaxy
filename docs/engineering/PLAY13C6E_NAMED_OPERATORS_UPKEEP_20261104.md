# PLAY-13C.6E — Named Oasis Operators and Equipment Upkeep

**Status:** implementation specification; not yet implemented or accepted. Parent: PLAY-13C.6D / PR #47. Continue on the existing stacked integration branch; do not merge until native Windows/Linux tests pass at its exact head.

## Objective

Replace the v76 ship-bound oasis extractor's aggregate `assigned_workers=2` proxy with named, living NPC custody and a repairable equipment lifecycle without introducing free workers, matter, autonomous bases, or teleportation. Keep the current player-built two-module installation, finite body-anchored resource reserve, solar charge, and physical ship intake.

## Strict invariants

1. The assigned operators are **exactly two distinct, existing, fit, adult NPC identities**, drawn from the installed extractor's actual owner ship crew registry. A count alone never authorizes extraction.
2. NPC `home_ship`, `location`, `transport_id`, `assigned_site`, casualty status and active medical status must reconcile with the real world-owned NPC records. No person may simultaneously work another incompatible shift/assignment, reside in an unreachable frame, be deceased or be aboard an active ferry.
3. Assignment and release are authenticated, world-authoritative and durable. Wrong owner, forged NPC IDs, stale commands and competing assignments produce no effects. Release does not create or relocate NPCs.
4. The extractor must remain installed at its original body. It operates only while the owner ship is in that body's valid frame, equipped, powered, crew-supported and with life-support reserves. No automatic worker transport or base construction.
5. Charging draws solar availability as in v76; operating cycles incur bounded wear, only from successful service ticks, and cannot silently repair the unit. A broken unit cannot collect resources.
6. Repair consumes **real ship-held manufactured spare modules**; transferred spare mass remains in installed equipment or persisted site scrap, preserving `total_ore()`. No free parts, workers, power or repaired condition.
7. Finite `OasisReserve` water/nutrient mass is debited exactly once per accepted withdrawal and credited only to the real owner ship. Partial SQLite failure rolls back inventory, source, charge, wear and labor state together.
8. Save version migration preserves legacy v76 records explicitly. It must not silently mint named worker IDs from aggregate counts; migrated rigs start unstaffed pending assignment.
9. The v76 prototype remains ship-deployed. Permanent surface base, farms, transportable rigs, independent local storage, and planetary nutrient-return shipments are separate subsequent gates.

## Proposed implementation sequence

### 6E.1 — Named assignment

- Add a persistent `OasisOperatorAssignment` mapping keyed by extractor ship and containing the two named IDs, assignment command receipt and tick.
- Host commands: `OASIS_ASSIGN|body|npc1|npc2|request_id` and `OASIS_RELEASE|body|request_id`.
- Validate current owner, deployed site, crew custody, skill/health and competing shifts *before* mutation; commit atomically through `PersistentWorld`.
- Update `OASIS_EXTRACTOR` telemetry to expose assigned named IDs and a precise blocked reason; never leak private ownership information to unrelated players.
- Reconcile NPC death, evacuation, migration, medical incapacity, ferry boarding and ship destruction. Assignment must become non-operational immediately if custody changes. Preserve identity history without resurrecting operators.

### 6E.2 — Wear and repair

- Add bounded `condition` degradation at deterministic intervals of *successful* operation; cap energy and condition integers, and define the behavior when condition reaches zero.
- Host command: `OASIS_REPAIR|body|request_id`. Require owner ship physically at body, named fit operators, spare manufactured module(s) physically aboard, and enough stored solar charge. Debit inventory and persist the mass in rig hardware/scrap; restore condition by a bounded amount.
- Failed repair and retry must not duplicate parts, energy or condition. Repeated identical request ID returns the original durable outcome.

### 6E.3 — Operation receipts

- Add a durable idempotency receipt for install, assign, release, repair and collection including owner, ship, body, resource/quantity and final outcome; rejecting reuse of the same request ID with a different payload.
- Coordinate with existing transaction/effect journal and recovery-copy mechanics; never invent a second persistence store.
- Preserve backwards-compatible host commands only if they can be mapped onto a safe request ID; otherwise require explicit new commands and clearly report deprecation.

### 6E.4 — Native acceptance

- Start with a real genesis ship, finite NPC identities, actual mined/refined/fabricated equipment, real source body, solar charging and initial life-support stocks.
- Assert reject cases: missing/duplicate/dead/injured/elsewhere/in-transit NPC, incorrect owner, competing site assignment, no power, broken rig, absent spares, nonlocal ship, insufficient life support, depleted source and forged IDs.
- Assert deterministic wear over ticks; physical spare consumption; zero mass drift; no duplicate source collection across retry and process crash; forced SQLite failure after snapshot write; restart and canonical-state round-trip; ship destruction and worker loss.
- Build and execute test on native Linux and Windows, not only a mock simulation. Capture exact HEAD commit SHA and complete workflow logs before declaring gate passed.

## Follow-on: 6F planetary farming and Cosmic Law

Use the approved planetary agriculture design: construct a physically located farm, feed it real water/nutrients, produce food from bounded recipes, ship food to orbital/space settlements, and record derived organic waste/wastewater/sludge as persistent local inventory for return shipment or explicitly accounted processing. Waste cannot vanish merely because a consumer is off-world. Return routing is a logistics gameplay requirement, not an instantaneous mass teleport.
