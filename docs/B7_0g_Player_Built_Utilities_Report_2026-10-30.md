# Single Galaxy — B7.0g Engineering Report
**Date:** October 30, 2026  
**Milestone:** Player-built solar utilities, finite services, equipment maintenance, and verified persistence.

## Implemented

- A newly constructed solar utility is physically attached to a player-built outpost and consumes **2 manufactured structural modules** (4 tonnes embodied); no utility is present at world genesis. Construction requires the founding colony ship near its owned outpost and authorization from its player.
- One utility generates at most **8 energy units per simulation tick**, stores up to **120**, and supplies **3 units per tick** to an operating staffed greenhouse and **2 units per tick** to an occupied habitat. Generation represents finite installed collector throughput from local starlight, rather than free physical mass; no orbital illumination model yet.
- A working, loaded utility loses **10 condition points every 20 ticks**, starting at 100. It can fail and stop generation. Stored energy temporarily supports loads before becoming exhausted, which stops automatic farm production and blocks household comfort progression. Staff continue consuming real supplies.
- Repairs require one **real manufactured module**, an authorized nearby colony ship and damaged utility. They restore up to 70 condition points and add the 2-tonne repair material to persistent embodied mass. Repairs cannot be performed remotely or by another player.
- Read-only Matrix Vision telemetry: condition, energy reserve, powered/outage flag, reliable cycles and repairs; validated local browser controls for build/repair. The local relay filters actions, schema, origin and session token. Neither the browser nor relay grants privileged admin actions.
- The viability checker now requires 3 consecutive powered service periods for every occupied outpost when utility service is installed. It does **not** declare full self-sufficiency or grant autonomous growth.

## Compatibility and durability

- **SQLite snapshot v13** with a canonical `utility` record; older v1–v12 checkpoints remain readable. The kernel preserves v13 when older actions execute afterward. A queued utility command is also restorable.
- Construction/repair are audited in the `resource_ledger` as `utility_construction_t` and `utility_repair_t` (4 and 2 tonnes respectively), cross-checked against the command journal. Tampering a receipt is detected at recovery.
- Original material mass is conserved through construction and repairs. Grid energy is modeled in its own game-energy units and is not conflated with physical mineral mass.
- **Legacy behavior is maintained:** B7.0f-era farms/habitats without new utilities still work under the previous provisional-power assumption, to retain historical regression coverage. This fallback **is not evidence of independent, self-sufficient utility service** and must be eliminated or explicitly migrated when the final starting-world power system is finished.

## Verification

- All **39/39** CTest native/integration suites passed on Linux in bounded sequential groups (1–10, 11–20, 21–30, 31–39).
- All **64/64** JavaScript tests passed, including 3 new B7.0g parser/control tests.
- B7.0g kernel test exercises construction, finite solar energy, occupied farm/habitat loads, 3-cycle viability, failure, interruption, physical repair, conservation, restart, independent journal copy, unauthorized requests and tampered-resource receipt rejection.
- B7.0g live test exercises real C++ host, saved utility telemetry, local relay allowlist, origin/session token rejection, malformed requests and actual served UI assets.
- Production-style Linux compilation with `BUILD_TESTING=OFF` succeeded. See B7.0g build/CTest/Node logs in `docs/`.

## Not yet implemented or validated

Native Windows execution, actual Chromium click-through, seasonal light/shadow or battery-chemistry physics, NPC technicians/repair jobs, automated repair procurement, equipment spare-part manufacturing chain separate from generic modules, grid topologies linking multiple sites, full autonomous colony governance, and continuous interstellar 3D space. B7.0g proves one local, player-built utility per outpost.

**Next sensible B7.0h direction:** dependable colony services and job delegation: authorized NPC maintenance schedules, physical spare-part logistics, affordable quality-of-life services, and transparent thresholds for self-sufficiency before permitting managed expansion. Avoid automatic prosperity or punitive login requirements.
