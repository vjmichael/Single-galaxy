# DEMOG-05q6e5b8g — Authenticated dispatch, diversion and settlement (v57)

**Date:** 2026-11-03. **Project:** Original Single-Galaxy C++17/SQLite simulation (not EVEmu).  
**Status:** Focused v57 kernel implemented, independently built, exercised, and archived. **Not yet merged into complete playable game / production multiplayer host / native Windows app.** GitHub is the repository/documentation destination, not the development host.

## Verified physical logistics

v57 extends the independently verified v56 three-owner, two-warehouse carrier route. A v57 policy flag is inserted **atomically with contract booking**. The carrier's own host-authenticated dispatch record requires three independently authenticated owner consents, a physically present carrier and exact reserved command identity before loading. The injected session-authentication function is a local fixture; no genuine client login, credential authority or cryptographic signature has been integrated.

A loaded shipment may receive an **immutable return-to-origin diversion**. The source owner, carrier and original receiver all sign the new route; the original destination unload is then prohibited. Return is a new, uniquely reserved host world cargo command, with actual finite ship cargo, local proximity, construction provenance and recorded source warehouse. The carrier must return physically through the reverse gate to its original star system and enter warehouse range. No cargo teleports, owner fields rewrite or shadow stock creates finished goods. **Alternate third-party destinations are not supported** by this focused return-to-origin variant.

Custody settlement is an immutable **two-party physical receipt acknowledgment**, not a currency or market payment. For successful delivery, carrier + receiver attest the canonical delivery ledger receipt. For physical return, source + carrier attest the canonical return receipt. The second signature atomically commits a final settlement record bound to command ID, world generation, world tick, destination site, material quantity and outcome.

The exercised scenario produces legitimate factory aluminum panels and frames at owner 7's constructed origin warehouse, loads both onto owner 8's independent carrier, and travels to owner 9's own constructed warehouse in system 2. **Panels are delivered**, then the carrier returns to the origin system and **frames are physically returned** to owner 7. The ship ends with neither shipment; world mass and source network escrow (8 kg) remain conserved.

## Host authorization, failure and replay acceptance

Tests reject anonymous/misidentified dispatch, incomplete consent, duplicate dispatch ID, pre-dispatch loading, inappropriate settlement roles, settlement before transfer, duplicate signatures, unauthorized alternate delivery after return request, return without three-party approval, premature or remote unloading, and command-ID collisions. At both canonical world failpoints, loading, unloading and returning roll back entirely and retry by original command ID. Independent SQLite reopen and genesis journal recovery preserve both final physical inventories and outcome-specific settlement records. Reopen rejects intentionally deleted or forged dispatch policy/record, return plan, return approvals, settlement votes, settlement receipt and inventory-ledger endpoints.

v57's insert-only tables are counted in authoritative `world_meta`; resource-ledger and historical commands remain physical custody truth. This is database consistency checking **not external cryptographic tamper-proofing**. Returned cargo releases its carrier/item reservation, so legitimate subsequent consignments are not stuck behind completed returns. An undelivered unapproved return remains on the carrier.

## Executed local test evidence

| Gate | Result |
|---|---|
| Strict GCC C++17 Release, complete CTest | **43/43 passed** |
| Debug UndefinedBehaviorSanitizer, focused v57 | **1/1 passed** |
| Verified source archive, internal SHA-256 | **341/341 files matched** |
| v56→v57 patch dry-run / independently applied source match | **4/4 changed files matched** |
| Fresh extracted source, C++17 rebuild (`-O0 -DNDEBUG`) and focused CTest | **1/1 passed** |
| Native Windows, real multiplayer identity and complete game merge | **Not run / not integrated** |

**Reproducible artifact:** `SingleGalaxy_DEMOG05q6e5b8g_DispatchDiversionSettlement_v57.zip`, **1,070,451 bytes**; SHA-256 **`77dafcef01fc48b57ceac89102f5c96894a4b715128da061bfab955175fe7e4f`**. Contains full focused C++17/SQLite source, 43 tests, v56→v57 four-file patch, release/UBSan/clean-extraction logs, acceptance report, and 341-entry SHA256SUMS.

**Source baseline:** v56 ZIP SHA-256 `8cc6a5444a21494ca7992d07dab94da67a3e8b21ad5a87023d54cccb34205d4f`, verified before extending. World physical host action retains v56 snapshot numbering, while immutable dispatch/return/settlement events use additive v57 SQLite tables.

## Remaining deployment gates

Real authenticated network/session ingress; alternative destination with new receiving-owner consent; partial/split lots; route expiry, forced cancellation while destination unavailable, and stranded-cargo policies; financial/market accounting if later authorized; cold/warm multi-region scheduling integration; machine/workforce/utility binding; full canonical game merger; native Windows installer/client acceptance. These are **not** covered by the focused test. This closes the v57 **local kernel acceptance**, not end-to-end production launch approval.

Dependent fatalities remain blocked. Children must never be visually depicted being harmed or dying; police/fire/institutional EMS remain deferred.
