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
