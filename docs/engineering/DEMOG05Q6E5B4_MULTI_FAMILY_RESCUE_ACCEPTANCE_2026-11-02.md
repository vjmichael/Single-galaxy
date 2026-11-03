# DEMOG-05q6e5b4 — Multi-family and split rescue acceptance contract (NOT yet implemented)

**Date:** 2026-11-02. **Baseline:** locally verified v45b3 focused simulator.

## Safety objective

A ferry with two or more independent ART02 family scopes must never execute an adult evacuation while leaving one family's child custody on the original ferry. Existing `step_with_dependent_rescue` is intentionally fail-closed when `active_dependent_berths(original_ferry)` is greater than the supplied one-family manifest count. This is a safety gate, NOT implementation of multiple-household rescue.

## Required construction

1. Bind each child to a canonical family scope, birth ID, original parent/guardian, legal current guardian if any, active berth, origin/destination region, and authenticated voyage/command; canonicalize ordering and require complete roster across **all** active child-bearing family scopes on the vessel.
2. Extend child boarding to accept multiple families under a single actual `HostEmbarkPassengers` world command; never duplicate a world step to append a second family.
3. Version/migrate `sg_dependent_transport_batch` from a globally unique `command_id` to compound `(family_scope, command_id)` so ONE real physical rescue command can have several separately attested family batches without invented synthetic command IDs.
4. Extend rescue to check all adult passengers, all children, safe receiving berths, every region authority token, and any authorized legal non-parent before *one* SQLite `BEGIN IMMEDIATE` world-step commit transfers all recognized survivors. Every old berth closes and a distinct receiving berth opens in the same commit.
5. Reject any partial/omitted manifest, duplicate child, conflicting parent/guardian, stale family-region authority, failed receiving berth capacity, mismatched incident case or mixed rescue outcomes that lack independently proven fate. Do not infer death or unaccounted status from missing SQL rows.
6. Test pre-write, after-header, after-child, post-snapshot, and post-receipt crashes; independently replay from genesis and reject forged batches, tampered nested manifests, missing child legal custody/parentage or duplicated active ferry berths.
7. Only after the above is verified may full multi-family rescue be marked complete; separation into different rescue ferries requires an additional documented custody-split contract.

**Presentation policy:** No visual depiction of children being harmed or dying. Fatal dependent outcomes remain disabled in current focused source. Police, fire and institutional EMS systems remain deferred.

**Related:** DEMOG05Q6E5B3AB_GUARDIAN_REGION_v45b3_2026-11-02.md.