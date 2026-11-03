# DEMOG-05q6e5b8b — Canonical warehouse and constructed factory integration (OPEN)

**Date:** November 3, 2026. **Status:** acceptance requirements only, NOT an implemented feature.

v51a demonstrates transactional conservation within a finite network of actual deterministic production lines, but its source material enters through a host-attested ledger seed rather than the game's constructed physical warehouse. This is the next required integration boundary.

1. Confirm a constructed warehouse, ship cargo, ore container or player-owned storage entity and physical material item ID in the canonical world. Reject nonexistent or unfinished construction, unowned access and impossible capacity.
2. A single privileged world+network transaction must withdraw a real ore/parts quantity from the canonical item inventory, mark its source custody as spent, and credit the network warehouse by exactly the debited amount. Unique receipt alone is not stock custody.
3. Add finite timed transport routes, ownership and storage capacity between constructed machines and warehouses; persist in-transit cargo and record arrival/cancellation so source, shipment and destination cannot simultaneously own it.
4. Register real construction entity IDs, machines, resources, utility service points and worker assignments against the current regional authority generation. Respect free/unbounded factory building geometry, rather than artificial factory floor area caps.
5. Bind actual power availability, maintenance/spares, medically available workers, shifts, training and multi-occupation proficiency to each machine's authoritative production admission.
6. Cold/detailed handoff must stage every constructed machine/WIP, depot, pending shipment, utility, workforce roster, and immutable effect under one owner fence. Compare with an independent always-cold control after mixed factory/transport events.
7. Inject crashes after each source debit, shipment creation, individual machine update, world step, receipt, and owner generation change; prove rollback/replay and no manufactured workers or materials.
8. Integrate into the original game main kernel and run physical Linux and native Windows acceptance: mine → haul → warehouse → smelt → manufacture → ship/sell → restart. GitHub remains a repository, not a required dev host.

**Safety:** Dependent fatalities remain blocked and no visual representations of children being harmed or dying. Full police/fire/institutional EMS remain deferred.

**Verified prerequisite:** `DEMOG05Q6E5B8A_TWO_MACHINE_PRODUCTION_NETWORK_v51a_2026-11-03.md`.