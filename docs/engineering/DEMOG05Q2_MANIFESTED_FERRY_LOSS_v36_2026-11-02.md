# DEMOG-05q2 — v36 Manifested Ferry Loss and Durable Casualties

**Date:** 2026-11-02  
**Project:** Original Single-Galaxy C++17/SQLite simulator (not EVEmu server)  
**Baseline:** Local DEMOG-05q1 v35 source bundle  
**Status:** Implemented and verified in local focused source bundle; **not merged into GitHub's game kernel or integrated with combat, full workforce/kinship simulation, or the player client**. GitHub is used only as a repository and engineering history.

## Local implementation

- New host-only `HostDestroyPassengerFerry` action and `galaxy-kernel-v36` snapshot. Terminal voyage status `6=Lost`; v34/v35 meanings preserved.
- Fatal command must name *all* people aboard a real active/stranded ferry, including the ferry's own native crew. No arbitrary death on a restart or hidden timeout. Strict refusal for non-ship worker roles, other simultaneous live voyages, dependent sites/away-assigned staff, and destruction that would leave a different home ship without a viable crew.
- Canonical `NpcCasualty` tombstones preserve full prior NPC identity/assignment/skills/mentor, source location, original transport ID, tick, host command ID and ferry. Living NPC IDs disappear from active counts but remain attributable historically. Home-ship living crew decremented for each fatality; the ferry's crew becomes zero.
- The ferry becomes a physically persistent `Wreck`; material mass remains accounted for; cargo is inspectable but the wreck cannot pilot or carry out ship actions. Wreck life-support cycles stop.
- Snapshot, individual deaths, wreck, population balances, command receipt and physical incident facts are committed through the existing SQLite journal transaction.
- Competing evacuation and ferry-loss actions use deterministic per-tick sequence plus command-ID ordering. The losing command revalidates and fails without a second custody/death outcome.

## Local test evidence

**11 of 11 Release CTest tests passed** (GCC 14.2, C++17, `-Wall -Wextra -Wpedantic -Werror`); **11 of 11 UndefinedBehaviorSanitizer tests passed** (`-fsanitize=undefined -fno-sanitize-recover=undefined`). Nine inherited v34/v35 tests remain green.

New `demog05q2_ferry_loss` exercises Bob rescued under a new voyage before a second voyage's explicit 7-person casualty manifest, living-population decrements, durable tombstones and wreck, DB restart, genesis replay, and both orderings of same-tick competing rescue/loss. New `demog05q2_ferry_integrity` covers failed commits and same-ID retries, missing/forged death or wreck state, manipulated snapshot incident receipts, missing native-crew death records, physically forged SQLite incident ledger and corrupted historical operation journal. All tested negative cases refuse.

## Remaining gates

- No authoritative damage resolver or automatic physical survival decision: the host supplies the outcome manifest.
- No partial rescue batches or mixed live/deceased result for one active manifest; full rescue must precede ferry loss as a separate journaled leg.
- No generalized farm/habitat worker deaths, complete family/genealogy/policy reconciliation, or conversion of legacy `HostCrewLoss` into the new death ledger.
- Separate global incident-ID registry, explicit `Evacuating` status, cold/warm passenger scheduler, ship salvage system, production GUI and Windows validation remain future work.

**Next:** DEMOG-05q3: mixed-survival manifested rescue and integration with genealogy/workforce/cold-warm authoritative population counts, ahead of a deterministic damage resolver.

The complete local source + tests + engineering report are provided to the user as the artifact `SingleGalaxy_DEMOG05q2_Manifested_Ferry_Loss_v36.zip`; it is **not yet part of the GitHub game-kernel branch**.
