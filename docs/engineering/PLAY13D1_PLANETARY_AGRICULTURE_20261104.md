# PLAY-13D.1 — Planetary Agriculture Mass Custody

Status: first production-source **domain kernel** wired into `galaxy_kernel`, native acceptance pending. This is NOT yet live-host gameplay, SQLite persistence, or a completed planetary settlement.

The new `galaxy::planetary::MaterialCustody` supports distinct body-bound stores and ship stores; water, nutrients, food, wastewater, organic waste and compost; bounded batch production; physical transfers and strictly conserved total kg. The domain model represents each shipment as a custody debit/credit, without pretending to implement travel, cargo capacity or player authorization.

Initial game recipes (mass-balanced abstractions, not literal chemistry):
- Farm: 10 kg water + 5 kg nutrients => 12 kg edible food + 3 kg wastewater.
- Crew consumption: 12 kg food => 8 kg organic waste + 4 kg wastewater.
- Composting: 8 kg organic waste => 6 kg compost + 2 kg wastewater.

These recipes intentionally do not yet convert compost back into nutrients. That will require a separately measured, explicitly conserved processing recipe. No free mass enters from sunshine: energy powers processes but is not represented as edible matter.

Test path: `tests/PLAY13D_PlanetaryAgricultureTests.cpp`. Build target `galaxy_play13d_planetary_agriculture_tests`. Platform matrix: native Windows and Linux.

## Next implementation gates

1. D.2: Bind real `PhysicalCelestialBody` IDs and authoritative owner/ship frame to site construction. Building materials must be manufactured and debited from a real ship hold, just as C.6D extraction hardware is.
2. D.3: Integrate material custody into `Simulation::canonical_state`, `Simulation::from_snapshot`, world-mass accounting and `PersistentWorld` transaction/rollback. Reject corrupted snapshots and repeated genesis.
3. D.4: Actual authenticated host commands with op IDs for arrival, physical cargo transfer, site building and farm cycling; prevent stale/duplicate requests and unauthorized cross-ship withdrawals.
4. D.5: Named NPC farmer and technician assignments, habitation, powered equipment, capacity, storms/operational interruption. No fictitious staff or free power.
5. D.6: Cargo manifests and physical planet-orbit return of waste with exact custody during transport, loss, recovery, restart and partial delivery. Implement provenance of originating planet to support reciprocal return policy.
6. D.7: UI planet selection, landing, farm controls, power/crew/water status, crop and waste shipment panels.

Do not merge this branch as a complete gameplay feature until those gates have native acceptance and the existing PLAY-13C stack passes.
