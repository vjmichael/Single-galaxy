# GRSM-03a — Local Social Groups, Attested Relays and Conflicting Group Views

**Single Galaxy | 2026-10-31 | local Linux source gate**

## Status

Implemented and exercised in the original Single Galaxy prototype. This is **GRSM-03a**, a bounded first slice of social memory, **not** an autonomous intercolonial faction simulation or a finished galactic reputation system. No EVEmu runtime code was added.

## Guarantees within this gate

1. **Real founders and overlapping affiliation.** A trusted host can found a named social group from 2–24 *existing*, living, co-located NPCs with one home colony ship. Members are sorted into a canonical, immutable founding roster; one NPC can belong to multiple groups. Groups are not preseeded at world creation, and migration makes no retrospective organizations.
2. **No telepathic broadcasts.** `relay_group_claim` requires a valid source claim held by one group member and a distinct, *currently living, co-located* recipient who also belongs to that same group. The new recipient receives an ordinary GRSM-02 provenance-linked claim. A separate `group_relays` record identifies the group context. Both records commit atomically in one SQLite transaction. Members absent from the conversation do **not** inherit that claim.
3. **Two competing views can persist.** `record_group_view` requires an actual GRSM-03 relay between two members during the current simulation generation/tick, both present at the meeting. It records a named narrative, confidence 0–100, and one of `Admiring`, `Condemning` or `Mixed`; multiple incompatible views are preserved. The view is **evidence of a local group discussion**, not a binding group vote, legal judgment, independently verified fact, or universal sentiment of everyone in the group.
4. **Immutable physical consequences.** Group formation, relays and views do not mutate ships, crew, resources, cargo, casualties, incident truth or historical command receipts. A fake narrative can be stored without changing the physical death count.
5. **Durable sequence and recovery.** SQLite `user_version=8` adds `social_meta`, `social_groups`, `group_relays`, and `group_views`. Append-only triggers, monotonically increasing IDs, chained FNV digests and counts/tips detect accidental mutation, deletion, missing leaf records and many invalid provenance links. GRSM-02 knowledge, GRSM-01b physical events, and older SQLite saves migrate without fabricated group history.
6. **Historic contact check with truthful limits.** When a founding or relay generation's immutable snapshot is still retained, startup re-checks original NPC co-location and existence. The older kernel retains only its **last three full checkpoints**; when earlier checkpoints are compacted, startup validates the durable append-only member/relay receipts and parent-claim provenance but cannot independently re-simulate physical co-location. FNV is **not** a cryptographic tamper-resistance system. Do not represent this as adversarially secure attestation.

## Scenario tests

`tests/GRSM03SocialTests.cpp`:

- Two real groups, *Crew Council* and *Mutual Aid*, overlap on NPCs 2 and 4; remote or invented NPCs and duplicate founders are rejected.
- A physical loss of one member is witnessed, faithfully relayed within Crew Council, and separately distorted within Mutual Aid. The same canonical casualty persists while one group records condemnation and the other admiration/mixed interpretation. NPC 5 does not know the story by membership alone; they acquire it only through a second physical contact.
- Out-of-group, remote, self-targeted, nonexistent group and ungrounded group-view attempts fail.
- Restart and `recover_copy` retain real crew headcount, physical incidents, group rosters, contradictory perspectives and individual claim ancestry.
- Tampered group membership, modified group sentiment, removed middle relay, removed last view and deleted group all refuse startup; legacy v7 migration creates no invented social records; stale concurrent writers are rejected.
- Previous migration-fixture tests (v1, v4, v5, v6) are updated to remove v8 tables when deliberately constructing older-schema SQLite saves, and the expected schema version is updated to 8.

## Local test evidence

- GRSM-02 and GRSM-03 targeted CTest: **2/2 passed** in Debug build.
- Previous GRSM-01, GRSM-01b and SQLite persistence fixture regressions retested after schema changes.
- **Final full native/integration CTest rerun: 58/58 passed** with `-j8` (25.08 seconds), including the legacy live-colony relay and the new GRSM-03 tests. The first parallel attempt had an intermittent listener-port failure (57/58), which passed independently, and another lower-concurrency attempt was interrupted. Preserve those failures as test-history evidence rather than erasing them.
- Full viewer/relay Node.js regression: **84/84 passed**.

## Open work — GRSM-03b onward

- Actual social-network edges, work schedules, household histories, membership joining/leaving, interpersonal trust, and factions forming without trusted host assignment.
- Connected infrastructure and bounded cross-colony news paths; no off-grid galactic broadcast. Communication outages, delays, source credibility and false-news challenges.
- Persistent institutional memory distinct from an individual discussion, with explicit contribution/ratification processes. Cohort/APS aggregation without O(all members) contact or O(all knowledge) startup replay.
- Subject identity and recognition checks, reputation dimensions (trust, admiration, fear, grievance, legal standing), seven *reach* levels, salience, rumor rarity, cooldowns and selective dialogue presentation.
- Durable compact historical contact certificates or checkpoint-independent verifier: the current small prototype only has host-audited append-only records once a detailed snapshot is pruned. Cryptographic authenticity and process security are out of scope.
- Live Windows builds, continuous 3D navigation and real player-triggered combat/hostile actor evidence remain unverified.

## Reproduce

```sh
cmake -S . -B build-grsm03-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-grsm03-debug -j8
ctest --test-dir build-grsm03-debug -R 'grsm0[23]' --output-on-failure
ctest --test-dir build-grsm03-debug -j8 --output-on-failure
node --test tests/*NodeTests.js
```

GitHub remains a source/provenance repository, not the primary local development or test host. Never merge this gate solely because a synthetic happy-path test passes.

## Archived verification transcripts

- `docs/GRSM03_ctest_58_of_58.log` — final full green rerun.
- `docs/GRSM03_node_84_of_84.log` — complete JavaScript test output.
- `docs/GRSM03_ctest_prior_intermit_57_of_58.log` — the earlier intermittent relay-socket failure (not silently discarded).
