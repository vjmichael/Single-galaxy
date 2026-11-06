# FLIGHT-05 — Physical NPC ferry rescue acceptance

**Status:** Source and Windows/Linux native test submitted on a stacked draft. No native acceptance claim until exact-head CI passes.

## Purpose
Connect FLIGHT-03 native physical ship-to-ship rendezvous and FLIGHT-04 read-only readiness to the EXISTING ferry emergency rescue executor, without introducing another custody system, bypassing player/host authority, or creating passenger identities.

## Native implementation
- `ship_interaction::submit_emergency_rescue_after_rendezvous` is a narrow guarded adapter in `ShipEmergencyRendezvous.hpp`.
- Requires the actual receiving owner and physical `NpcFlightMode::LocalRendezvous` arrival plus matching native `NpcRendezvous` activity and current safe proximity/relative motion.
- Requires an original `Action::HostEvacuatePassengers` with `Simulation::kServerAuthority`, nonzero distinct source/receiver, real source voyage, nonempty passenger manifest and next committed tick.
- Delegates directly to `PersistentWorld::submit_host_emergency_ferry_rescue`, retaining original critical/emergency risk assessment, exact passenger manifest, receiving crew and life-support reserves, active voyage, available berth, anti-duplication, SQLite atomic posting and host-only custody authority. A successful submit is NOT completed rescue.
- The existing world step, original native event and original ferry voyage records still own the custody transfer. No new passenger records or independent vehicle controller.

## Acceptance
`tests/FLIGHT05_PhysicalRescueTests.cpp` extends the original `COLONY_AUTO04D3_FerryRescueTests.cpp` scenario:
1. Four real named passengers enter an original ferry; original host boarding and voyage record apply.
2. Healthy-ferry emergency authority refuses; source eventually becomes critically understocked.
3. Zero rendezvous and wrong owner both refuse admission; SQLite faulted rendezvous posting rolls back.
4. A qualified original NPC pilot on the actual receiving ship approaches source through the FLIGHT-03 shared navigator.
5. Real native completion receipt unlocks the *request to the original host rescue executor*, never the rescue itself.
6. Incomplete manifest, wrong owner, and a SQLite failed submit are refused without changing original custody.
7. Original applied evacuation changes exactly four passenger custodies in native world; the original rescue voyage is durable and source crew are not inadvertently moved.
8. Existing material conservation, life-support consumption and close/reopen SQLite checks continue.

Windows/Linux native workflow runs FLIGHT-05, the original AUTO04D3 emergency rescue and FLIGHT-03 native rendezvous. No passing CI or installed-client claim yet. Dependent child guardian-specific rescue retains its separate original `step_with_dependent_rescue` verification and remains a future positive combined physical gate.
