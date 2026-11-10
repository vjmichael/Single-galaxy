# WO3-F — Original historical SQLite migration contract

Decision date: 2026-11-10. Applies to the original Single-Galaxy native
`Simulation` / `PersistentWorld`, not the separate EVEmu learning project.

## Source-derived compatibility boundary

The exact historical kernel commit
`d2cf352346a0574225e156f89e1baf1ac8ed9bd9` (2026-11-05) has the
following demonstrated behavior:

- Its original native planetary test saves the actual SQLite command journal
  as **`galaxy-kernel-v85`** after one physically landed shuttle visitor.
- Its **`Simulation::upgrade_legal_residence_snapshot_format()`** converts
  a v85 in-memory snapshot to v86 only when NPCs have valid colony-ship homes;
  it explicitly sets each legal residence to `home_ship` and does **not**
  move anyone to a planetary legal address.
- Its original native test checks the v85 SQLite snapshot and separately
  verifies that an in-memory copy upgraded to v86 preserves the legally
  authoritative **colony ship** home while the person is physically present
  on a planetary surface.
- No source-proven native action from the reviewed pre-v109 producer code
  grants `LocationKind::PlanetarySite` legal residency. The v109+ WO3
  `settle_planetary_founder` pathway is the first demonstrated lawful
  producer of that address. Therefore a **positive pre-v109 importer test**
  requiring an original legal `PlanetarySite` address cannot be satisfied by
  pretending that physical disembarkation implies permanent immigration.

Keep the compatibility importer fail-closed for any unproven older format.
Do not manufacture a v86 SQLite database with a v109 legal address, patch
historical snapshots, bypass genesis checks, or claim a new reproduction is
a signed/archive-era artifact.

## Authenticated native reproduction acceptance

The historical test source is Git blob
`23516163160b4e8730553371eb04666082ae6231` at the pinned commit.
`tests/WO3HistoricalNativeProducer.py` checks these two identities before
preparing the original source. It adds an observation-only checkpoint to
the **historical original test harness**, never to its kernel or persistence
libraries, so `PersistentWorld::recover_copy` captures the native v85
SQLite journal **after original approved command execution**, not a
synthetic/new genesis.

The resulting fixture is a **new reproducible historical-source native
SQLite artifact**. It is *not* an extant archived 2026-11-05 SQLite database
and its instrumented test executable is *not* the original archived binary.
The attestation manifest explicitly sets `archived_artifact: false`.

Every Windows/Linux acceptance rerun requires:

1. The exact original kernel commit and original test Git blob, with no
   non-test source file edited in the historical checkout.
2. The compiled native producer binary and its SHA-256 and the original
   native SQLite bytes and SHA-256.
3. The provenance manifest and read-only SQLite `integrity_check`,
   original generation-1 tick-zero operation, real accepted original
   gameplay commands, authoritative receipt rows, and native v85 header.
4. `PersistentWorld::recover_copy` verification of source journal,
   checkpoints, commands, receipts, resource conservation and original
   NPC/ship/planet physical custody.
5. Actual historic v85→v86 `Simulation` upgrade with no new humans,
   no material mutation, legal residence remaining the genuine colony
   ship, and a complete independent snapshot roundtrip.
6. Unauthorized and impossible legacy import rejection without changing
   the SQLite generation or state.
7. Independent SQLite restart with the same canonical state, hash, ore,
   total material and original NPC/legal-address identities.
8. Separate full WO3 housing/utility v109+ owner-consent settlement positive
   checks, WO4 stock allocation, and WO5 freight/custody/recovery tests.

The consolidated 33-test native gate includes this historical test. A
missing producer, mismatch in the pinned source/binary/SQLite/manifest, or
failure of journal verification is **failed acceptance**, not skip or
synthetic substitution. All evidence must match the current exact-head
WO5 branch on both native operating systems before the task group closes.
