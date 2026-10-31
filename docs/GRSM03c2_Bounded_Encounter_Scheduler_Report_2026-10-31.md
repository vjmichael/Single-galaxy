# GRSM-03c.2 — Bounded Recurring NPC Encounter Receipts

**Single Galaxy | 2026-10-31 | locally developed C++/SQLite prototype**

## Scope

This engineering slice adds a host-invoked, deterministic **opportunity to meet**, not automatic friendship, political sentiment, rumor propagation, conversation text, transport, or a simulation-wide NPC update loop. It remains isolated from EVEmu archival code, Lu, and Cognitum.

## Implemented

1. **SQLite schema 11, non-inventing migration.** v10 saves upgrade atomically with empty encounter tables. No encounters are backfilled from old worlds, old group stories or historical deaths.
2. **Host-only bounded API:** `PersistentWorld::schedule_social_encounters(scan_budget, contact_budget)` accepts 1..64 candidate pair checks and 1..8 resulting contact receipts per call. It scans actual *living* individual NPC identities from the current authoritative simulation, not synthetic cohort members. Population cap is 1,024 NPCs for this prototype; larger populations fail explicitly instead of silently turning into an unbounded all-pairs loop.
3. **Fair, deterministic iteration:** A persistent round-robin cursor indexes canonical unordered NPC pairs. A call resumes after the prior scan and examines only the permitted budget. Pair lookup does not materialize the whole quadratic pair set. When the roster changes, the cursor is interpreted modulo the new pair count.
4. **Real physical co-location:** A candidate pair receives a contact only if both NPC identities are in the authoritative world and share the same named location. NPCs on separate colony ships do not meet. Meeting opportunities are not proof that they had a particular conversation or witnessed a historical incident.
5. **Contact cooldown and familiarization:** The same unordered pair cannot receive another contact receipt until at least eight simulation ticks have passed. Every recorded encounter includes an ordinal (first, second, etc.) and prior-contact pointer. It is **evidence of contact frequency**, not a free trust increase. Directed trust remains controlled by GRSM-03b evidence-linked reassessment.
6. **Atomic, append-only records:** Every invocation writes a scan receipt; accepted contacts are stored in the same immediate SQLite transaction. Two separate FNV-linked audit chains, row counts, scan cursor and immutable-row triggers detect ordinary missing, edited or reordered records. Failed transactions do not advance the cursor or create partial contacts.
7. **Restart/recovery validation:** On load, replay verifies cycle order, limited scan sizes, canonical cursor progression, event counts, per-pair prior IDs and ordinals, cooldown, digests, and surviving physical-snapshot proximity/identity wherever available. `recover_copy` preserves these checks and original material, life and casualty state. Checkpoint compaction can remove old full snapshots; historical co-location then rests on the host-attested append-only receipt rather than reconstructed pixels/positions.
8. **Read-only bounded query:** `recent_social_encounters(limit)` fetches at most 128 newest records without changing world history.

## Regression tests and evidence

- `tests/GRSM03c2EncounterTests.cpp`: bounded deterministic scan behavior, remote-colony exclusion, repeat cooldowns, two contact rounds with familiarity ordinals, persistence/recover-copy, evidence isolation, deliberate record edits/deletions/cursor tamper, and a real schema-10-to-11 fixture with preserved group membership.
- Prior tests which deliberately downgrade a database now drop the v11 tables as part of constructing *genuine* older schemas; B2's migration assertion expects schema v11.
- Four prior intermittently failing Python relay suites now use monotonic, bounded listener readiness checks with early-exit diagnostics; B6.2c waits for an actual authoritative undock event rather than a fixed sleep before issuing a dependent jump command. The relay runtime authorization and network scope are unchanged. The root causes of all historical intermittent failures are not fully established.

### Local verification summary

- Native C++ target and its two scenario families: **passed**.
- Native persistence + encounter targeted regression: **2/2 passed** after v11 expectation fix and restore hot-path optimization.
- Four previously unstable relay suites run together: **4/4 passed** against the previous host build with improved Python harness.
- Full Linux Debug native build: **passed**; final post-fix full CTest: **61/61 PASSED** (`docs/GRSM03c2_Final_CTest_61_of_61.log`).
- Node.js client/relay tests: **84/84 passed**.
- Python design tests: **3/3 passed**.
- Initial broad CTest: **60/61**, due to a stale v10 schema assertion. After correcting the fixture, the full CTest passed **61/61**. A later complete run found a B5.2 station timing race (**60/61**); using authoritative SSE state barriers instead of sleeps repaired it. The subsequent full final-code run was **61/61**, with no failed tests.
- Four previously troublesome relay tests: **12/12 passes** across three consecutive parallel four-test runs after replacing fixed readiness sleeps and the B7.0d direct agricultural sleep with a deterministic plant/save/restart/harvest test. The failed pre-fix agricultural stress run is retained, not erased.
- The B5.2 station test now waits for authoritative undock, cargo-full, dock and hangar-transfer frames before issuing dependent actions; earlier failing trace retained in `docs/GRSM03c2_Pre_Station_Sync_CTest_60_of_61.log`.
- Observed passing results improve confidence, but are not evidence that *all possible* relay timing bugs have been eliminated, nor that Windows or an interactive Chromium UI was exercised.

## Remaining limitations

No automatic NPC conversation content or gossip is generated. No contact creates a social tie in GRSM-03b or changes trust, group membership, legal standing or world goods. Multi-colony travel and communications topology are still unimplemented. Prototype replays append-only history on startup rather than using scalable APS cohort aggregates or incremental cryptographic checkpointing. FNV digests are corruption detectors, **not** security authentication. No native Windows or Chromium playthrough is claimed.

## Reproduce

```sh
cmake -S . -B build-grsm03c2 -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-grsm03c2 -j4
ctest --test-dir build-grsm03c2 -j4 --output-on-failure
node --test tests/*NodeTests.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

## Next work

Move from host-invoked encounter registration to event/sector-prioritized scheduling, explicit attendance contexts (work / home / recreation), and evidence-linked social reaction decisions. Keep physical route authorizations, trust, group views, notoriety, personal recognition, and institutional ratification separate. Test for starvation under changing populations and investigate any remaining intermittent live relay scenario failures.
