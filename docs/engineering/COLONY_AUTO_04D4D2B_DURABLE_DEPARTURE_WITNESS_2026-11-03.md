# COLONY-AUTO-04D.4D2B — Durable pre-departure family custody witness

Date: November 3, 2026. Branch: `integration/colony-auto04d4d2b-predecessor-pin-v75-20261103`.
Base: v74 candidate for full atomic family movement (PR #8, commit `b1cd1d3618124c0a5198d25d8a5c60e9d16582af`).

## Root cause
The v74 physical family/ART02/ECON03 transaction itself passed local native checks, but independent recovery later refused with `SQLite: missing pre-departure generation 641`. A completed family evacuation's historical verifier requires the exact world snapshot from the **generation immediately before movement**, to check that the child and both parents were physically at source, and that the originally prepared intent's world hash was authentic. The ordinary rolling-snapshot policy keeps genesis + three latest versions, so after 19 additional ticks it deleted this indispensable physical evidence.

## Change
The canonical Store::commit pruning policy now pins precisely `sg_colony_family_departure_execute.world_generation - 1` for each completed family evacuation (in addition to earlier protected incidents). This protected predecessor is chosen **after the custody effect is written, within the same SQLite writer transaction**. No new table, shadow population ledger, or fabricated NPC is introduced. Failed atomic effects do not publish a protected receipt. Unrelated snapshots still obey the existing three-recent plus genesis policy.

The unit test now asserts that the **original predecessor snapshot survives after long-running simulation**, remains in the independent `recover_copy` image, and that deleting this predecessor deliberately causes the reloaded saved world to **fail closed**. This is a narrow evidence-retention fix; it does not replace mathematical/cryptographic verification of a privileged database editor.

## Related gates
- v73 PR #7: prepared ART02 family proof, not a rescue.
- v74 PR #8: candidate actual physical family movement; independent replay had previously failed (6/7 native tests).
- v74 PR #9: independently tested real child occupancy and needs, not yet whole-family movement.
- v74 PR #10: cold/warm and immutable-manifest execution fencing; independent seven-test native suite passed.

These branches overlap. Integrate only through the appropriate tested descendants, respecting each branch's distinct history. Do not label the larger full-family movement accepted until **its** execution suite passes on the combined head and further cold/warm and recovery acceptance is complete.
