# DEMOG-05q6e5b8d — Physical finished goods distribution and constructed machine binding (OPEN)

**Date:** 2026-11-03. **Status:** Engineering acceptance contract only; not implemented in v53.

v53 has a verified two-way transfer between one constructed outpost store and one regional production-network warehouse. It does not yet demonstrate successfully shipping the returned goods to a ship or remote colony.

## Required executable acceptance

1. Bind a fully constructed warehouse site, its finite capacity and exact item-code inventory to the authoritative world; no phantom storage or stale ownership.
2. Move specific world goods (e.g. aluminum panels or frames) into a real ship's cargo with a host-validated load order, item code, capacity, pilot/crew access and actual physical docking/proximity state. Refuse full cargo without decrementing source.
3. Persist in-transit goods with a single ship/transport owner and immutable identity; no free instant delivery across systems. On unloading at an independently built destination store, debit ship cargo and credit the destination in one authorized world operation.
4. Record provenance from mined material → factory output → world return → ship cargo → destination storage. Keep world mass and regional inventory conserved through restarts, interrupted loading, transit and unloading.
5. Bind constructed factory equipment IDs, machine recipes, utilities, maintenance, actual staff, resource stores and job proficiency to the authoritative world, not merely host-staged network machine identifiers.
6. Promote and demote cold/detailed multi-site production and warehouse/shipping effects without duplicate production or double-applied transport events.
7. Test failed transactions after each debit, receipt, world snapshot and owner transition; independent genesis replay, missing receipt corruption, attempted duplicate item movement and stale owner tokens.
8. Integrate into the game's canonical kernel and run real native Windows and Linux game acceptance; GitHub remains a repository and not a development requirement.

**Safety policy:** dependent fatalities remain blocked; children must never be visually depicted being harmed or dying. Police/fire/institutional EMS remain deferred.

**Verified prerequisite:** `DEMOG05Q6E5B8C_TWO_WAY_WORLD_INVENTORY_v53_2026-11-03.md`.