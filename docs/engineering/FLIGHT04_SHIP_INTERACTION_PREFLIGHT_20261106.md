# FLIGHT-04 — Ship-to-ship physical interaction preflight

## Scope
This additive read-only gate separates *navigation completion* from *interaction authority*. It reuses the same Simulation, native ship entities, FLIGHT-03 flight order and activity receipts, and universal transport manifest model. It does **not** create independent cargo/crew state or grant an operation.

`spatial_preflight` checks real ship identities, ship owner, same local frame, native dock/jump status, physical separation and relative speed. Its arithmetic is overflow-safe and portable to Windows MSVC.

`rendezvous_preflight` requires those current physical checks **and** a completed LocalRendezvous flight record with matching owner/actor/target plus an original `ActivityKind::NpcRendezvous` receipt tied to that flight's actual last native command. A false or stale arrived flag is insufficient. Readiness is only an input to the interaction-specific authoritative executor.

## Separation of authority
- Navigation owns original Thrust and per-tick movement only.
- Readiness owns no action and does not move any people or inventory.
- Freight calls the existing native material custody/hold/capacity/consent gates.
- Rescue and passenger transfer call existing individual identity, guardian, transport and recovery gates.
- No automatic boarding, docking or unloading follows from approach.

## Verification and dependency
The FLIGHT-04 native CTest includes owner checks, self-target rejection, out-of-range rejection, overflow bounds, no fabricated original activity or stock change, and no interaction permission without a recorded rendezvous.

FLIGHT-04 is **stacked on draft FLIGHT-03 PR #66**, whose main Simulation.cpp rendezvous execution is not yet committed. That dependency leaves the current stack non-acceptance-ready and may prevent linking the native test executable until FLIGHT-03 is completed. Keep both PRs draft; do not claim live ship-to-ship transfer.

The next gate must integrate this read-only preflight into each EXISTING native interaction executor and demonstrate authorization, real custody movement, crash/replay, and zero duplicate transfer.

## FLIGHT-04B — Existing executor adapters (source committed)

`ShipInteractionExecutors.hpp` now offers **explicit** guarded entrypoints for:
- `deliver_freight_after_rendezvous`: checks the genuine freight leg, owner, geometry and receipt, then calls existing `PersistentWorld::deliver_intercolony_freight` to retain original sealed hold, resource amount, destination supply capacity, celestial-arrival receipt and SQLite rollback logic.
- `submit_passenger_rescue_after_rendezvous`: accepts only the original **server-authority** `HostEvacuatePassengers` command with actual source and receiving ferry, and calls the existing `submit_host_passenger_action`. This posts rather than fabricates a completed rescue.
- `execute_dependent_rescue_after_rendezvous`: calls original `step_with_dependent_rescue` only with a physical rendezvous receipt and a complete original dependent-rescue plan. Existing guardianship, identity, family-version, voyage and SQLite custody checks still own the effect.
- `execute_dependent_arrival_after_rendezvous`: narrowly requires a nonboarding original `DependentTravelPlan` whose named destination is the receiving physical ship, before using the same original `step_with_dependent_transport` authority.

New `galaxy_flight04b_interaction_executors` native regression establishes fail-closed behavior for missing rendezvous (no freight debit, no passenger move, no command or activity fabricated); the Windows/Linux workflow includes this fixture.

**Important migration and integration caveats:**
1. These are additive **entrypoints** only. Legacy planetary, station, celestial and emergency rescue operations intentionally remain on their existing paths and cannot suddenly require ship rendezvous.
2. Existing intercolony freight also requires a **celestial transit arrival receipt**. Merely arriving alongside another ship is intentionally insufficient for delivery; future same-frame freight must be implemented under the original sealed goods transaction without discarding those original custody invariants.
3. The trusted gameplay host/dispatcher does not yet invoke these new entrypoints for live orders. Wiring dispatch must come after FLIGHT-03's missing core LocalRendezvous execution/receipt/snapshot implementation.
4. Because parent PR #66 still declares LocalRendezvous functions without implementing the core, native compiling/linking or passing CI is **not claimed**. No acceptance, merge or operational ship-to-ship passenger/freight claims before it is repaired and tested.


## FLIGHT-04B full physical freight acceptance source (November 6, 2026)

This branch is now merged with FLIGHT-03 v97 native ship-rendezvous code and has an additional **positive** regression named `galaxy_flight04b_real_freight`. It extends the existing PLAY13D / WORK-01C native sealed-freight fixture rather than creating any competing material ledger.

New `--flight03-rendezvous-freight` scenario:
1. Uses genuine finite planetary output and the existing original station/colony source-to-ship sealed cargo transaction.
2. Uses a qualified original NPC pilot and a real native celestial flight to the receiving body's physical frame (maintains original intercolony-voyage requirement).
3. Posts a separate authorized `LocalRendezvous` to the destination **physical ship** and waits for matching position, velocity and the committed `NpcRendezvous` native activity.
4. Refuses early transfer and wrong-owner transfer through `ship_interaction::deliver_freight_after_rendezvous`.
5. Injects a native SQLite publication failure on the proposed transfer and asserts unchanged canonical inventory/world state.
6. Completes the **original** `PersistentWorld::deliver_intercolony_freight` through the rendezvous-guarded entrypoint, proves exact receiver stock and sealed hold debit, and rejects duplicate delivery.
7. Continues existing physical return-voyage, inventory conservation, snapshot roundtrip and persistent SQLite close/reopen checks.

Existing legacy and delegated freight scenarios keep their original commands and do not acquire surprise rendezvous prerequisites. Passenger and child rescue still require their original host/guardian/manifest executors; no fabricated positive passenger rescue is claimed.

**State:** Test and source wired, Windows/Linux native verification queued. Do not merge or mark acceptance green before the exact head passes.
