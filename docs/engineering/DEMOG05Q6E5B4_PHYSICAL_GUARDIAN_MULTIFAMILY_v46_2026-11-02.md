# DEMOG-05q6e5b4 — v46 physical non-parent custody and atomic multi-family rescue

**Date:** 2026-11-02
**Project:** Original Single-Galaxy C++17/SQLite simulation, not EVEmu.
**Baseline:** Verified focused v45b3 non-parent legal guardianship + region-scoped custody.
**Status:** Implemented, strictly compiled and tested locally as focused v46 source; **NOT merged into the full game main kernel, playable client, or Windows release**. GitHub is source/documentation repository only; local Linux builds provided verification.

## 05q6e5b4a — physical custody from two parents to a designated non-parent

- New host-only `DependentPhysicalHandoffPlan` records a physical escort reassignment only after an independently verified and already committed `NonParentGuardianshipPlan` legal transfer. Both recorded parents, the real non-parent escort, and every listed dependent must be physically co-located aboard the authenticated receiving rescue ferry at handoff.
- Exact, sorted and exhaustive ART02 child roster, stable world generation, real ferry/voyage ID, immutable legal transfer ID and actual guardian NPC ID are required. An authorized legal guardian is not retroactively written into the birth certificate or original ferry boarding record.
- One SQLite `BEGIN IMMEDIATE` transaction inserts immutable physical handoff header, all child member receipts and a durable metadata count anchor. Failure after header or child receipts rolls back fully. Safe retry does not clone children or introduce an extra physical custodian.
- `verify_dependent_physical_custody` independently validates physical assignment, authoritative guardian, original rescue attestation, parents, historical world snapshot, current ferry/arrival custody, counts and immutability triggers. The world writer now checks all active physical-escort assignments **inside each world commit**, rejecting state updates that would separate the designated escort from a dependent. Historical handoff snapshot generations remain pinned against pruning.
- Explicit limit: this assigns onboard physical escort/custody; it does not turn children into adult NPC workers, move them into an unrelated ferry, or provide an autonomous guardianship consent/court UI.

## 05q6e5b4b — multiple distinct households on the same physical ferry

- New `DependentFamilyManifest`, `MultiDependentTransportPlan` and `MultiDependentRescuePlan` support two through eight distinct sorted family registry scopes, bounded by real 16-person ferry berth capacity.
- One genuine host boarding, arrival or rescue command carries every adult guardian and dependent family. Each family must provide a complete, unique ART02 child roster, authenticated birth parentage, origin residency, physically co-traveling named adults, family seed, region scope and Stable Cold owner generation.
- For a rescue, every current child berth on the original ferry **must** be in the union of submitted family manifests; source child berths close and all children receive receiving-ferry voyage legs **within the same SQLite world transaction**. A missing family, invented child, duplicate guardian, stale owner or insufficient receiving berth capacity rejects the operation before publication. A failed SQLite commit leaves all families on the original ferry.
- `sg_dependent_transport_batch` upgrades transactionally from globally unique command IDs to a `(family_scope, command_id)` composite key, preserving old rows, so multiple family receipts can correctly point to one real applied ferry command. Restore checks each family receipt against applied command action, voyage/transport, immutable world operation log and corresponding actual child travel legs.

## Verified tests and artifact

| Verification | Result |
|---|---|
| Strict GCC C++17 Debug full CTest | **32/32 PASS** |
| Strict GCC C++17 Release (-O1) full CTest | **32/32 PASS** |
| Debug GCC UndefinedBehaviorSanitizer full CTest | **32/32 PASS** |
| Independently copied source; fresh Release build + all tests | **32/32 PASS** |
| Final ZIP extracted-file SHA-256 checks | **223/223 PASS** |
| v45b3 → v46 source patch dry-run against pristine v45b3 | **PASS** |
| Final ZIP SHA-256 | `4b4d3b29088a572ef0f61c348b02930f76507f83cdf541f17551cb1a942fdf40` |

**Source archive:** `SingleGalaxy_DEMOG05q6e5b4_Physical_Guardian_Multifamily_v46.zip` (672,991 bytes). Includes complete focused CMake source, patch, 32 native tests, engineering report, local/clean-build and sanitizer logs, and 223-entry integrity manifest.

New physical-handoff test: 3 real adult passengers, 2 children, two parental approvals, completed non-parent legal designation, host physical escort transfer, injected failed commit and retry, database restart, wrong guardian refusal, incomplete child-manifest refusal, duplicate transfer refusal, and removed physical child receipt rejection.

New multi-family ferry test: 2 independent ART02 families, 4 original parent NPCs, 2 children; single real shared boarding command and ferry; stranding, single real multi-family rescue command and receiving ferry; injected failure preserves original ferry custody; one missing family and one forged child refused; final arrival, restart, independent genesis journal recovery and erased one-family rescue batch rejected. The regression uses two separately enrolled family/region authority scopes sharing a physical ferry, not yet a production-wide unified family census.

## Remaining gates — do not claim these are finished

- Splitting rescued families or children between **multiple receiving ferries**, distinct route/time destinations, temporary escort care with parents absent, independent NPC-consent/court policy, and transfer between non-parent guardians are NOT implemented.
- Multi-family family-census/region-population consolidation within one real production region, detailed/warm workforce, manufacturing and economy checkpoint promotion/demotion, and multi-database commits remain open.
- Full game kernel merge, actual graphical client, native Windows tests, external cryptographic tamper sealing are not claimed.
- **Fatal dependent incidents remain disabled, and no children may be visually depicted being harmed or dying.** Full police, fire, and institutional EMS remain deferred by user direction.

**Next:** multi-family unified population and split-ferry receiving-custody protocols, then real warm/detailed regional workforce and industrial checkpoint staging.