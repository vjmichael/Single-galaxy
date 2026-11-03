# Independent v45b3 rescue outcome and receiving-guardian verification

**Date:** 2026-11-02. **Scope:** original Single-Galaxy focused C++17/SQLite kernel, not EVEmu or the full game client.
**Status:** Independent re-verification of unchanged existing source. Not a new feature, main-kernel merge, or native Windows build.

## Reproducibility

- Source archive: `SingleGalaxy_DEMOG05q6e5b3ab_NonParent_Region_v45b3.zip`, SHA256 `ef4ea70e28d308c319a3721441d60abb6dc117020ab7b3177536eb54766deb5e`.
- Final archive source checksum manifest verified after fresh extraction: **217/217 matched**.
- Fresh extracted-source CMake Release configure and build: **success**.
- Fresh extracted-source CTest Release: **30/30 passed**.
- Rerun of previously built Debug UndefinedBehaviorSanitizer suite: **30/30 passed**. This UBSan binary set was not separately rebuilt in this verification.
- Direct execution of `demog05q6e5b2_rescue_outcomes` and `demog05q6e5b3a_nonparent_guardian`: **both PASS**.

## Facts demonstrated by the tests

1. **Historical rescue:** the authenticated nonfatal stranding case matches the actual applied evacuation command, full child roster, source and receiving transport IDs, pinned world journal/snapshot, and actual receiving child berths. Failed outcome writes roll back atomically. Later stranding, arrival, restart and independent genesis recovery do not rewrite the historical rescue.
2. **Receiving-parent custody:** the rescue outcome's receiving guardian is an actual ART02 parent physically aboard the receiving ferry when rescue is attested. Missing, invented or non-aboard parent IDs are refused.
3. **Separate legal non-parent designation:** an unrelated real NPC on the same receiving ferry may receive an immutable legal designation only when each existing parent independently approves the identical complete two-child manifest. Failed approvals/transfers roll back; birth parentage remains immutable. On reopen, replay and receipt validation, the designation remains the same.
4. **No false physical transfer claim:** legal non-parent designation is **not** physical escort reassignment. Child ferry records retain their original primary and co-parent custody. An explicit physical-handoff event for an unrelated guardian remains unimplemented.
5. **Tamper refusal:** adversarial tests reject changed receiving-guardian IDs, fake parent approval, deleted child manifests, missing rescue/guardian receipts, missing pinned historical snapshots, altered transport predecessor links and forged world metadata counters.

## Open work

- Full legal-to-physical non-parent child escort transfer, mixed/split multi-family rescue, and real warm/detailed world promotion with workforce and industry checkpoint reconciliation remain unimplemented.
- Host records approval as trusted authorization; independent NPC preference/consent evidence, courts and appeals are not yet modeled.
- Child-bearing fatality commands remain disabled; children must never be visually depicted being harmed or dying. Police/fire/full institutional EMS remain deferred.
- These checks do not establish externally tamper-proof cryptographic sealing or Windows/playable-client integration.

**Outcome:** Rescue outcomes and *receiving-parent physical custody* are independently verified. Non-parent *legal guardian designation* is verified. Full physical custody transfer to a non-parent is **not yet implemented and must not be described as verified**.