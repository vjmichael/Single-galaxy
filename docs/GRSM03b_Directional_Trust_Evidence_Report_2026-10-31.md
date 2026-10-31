# GRSM-03b — Directed Social Contacts, Trust-Weighted Testimony, and Durable Recovery

**Single Galaxy | 2026-10-31 | locally implemented Linux engineering slice**

## Scope

This milestone adds a **bounded, explicitly host-driven social-relationship vertical slice** to the existing physical-history, NPC-evidence and group-conversation components. It does **not** implement general autonomous gossip, NPC-AI reasoning, self-organizing factions, work-shift schedules, or the seven-level reputation reach system. It is original Single Galaxy code; legacy EVEmu material remains archival research.

## Rules implemented

1. **Real meetings:** `record_social_contact(observer, source)` starts a directed tie only between two distinct living NPCs at the same actual location in the current simulation. The reverse tie is **not** implied. Initial subjective trust is 50/100, not an objective measure of credibility. Duplicate contacts, remote NPCs, ghosts and invented identities are rejected.
2. **Evidence-linked reassessment:** `reassess_social_trust(observer, source, claim_id, delta)` requires a physically co-located pair and a real, personally received, non-firsthand claim **spoken by the named source to that observer**. A single claim may be evaluated at most once. A subjective change is bounded to [-25,+25]; the resulting trust is clamped [0,100]. The system does not treat approval/disapproval as proof of the claim's historical accuracy.
3. **Contact- and trust-gated relays:** `relay_trusted_claim(parent_claim, listener, ...)` requires current co-location and the listener's own tie **toward** the speaker with trust at least 25. The resulting recipient-specific knowledge entry retains the immutable incident and parent provenance, and its confidence is bounded by the proposal, the source account's confidence, and the recipient's trust. Trust and source confidence are checked **inside the same `BEGIN IMMEDIATE` SQLite transaction** that inserts the new claim; a concurrent trust update cannot silently bypass the gate.
4. **No mandatory blanket social authority:** This is an **opt-in trusted relay path**. The older host-controlled GRSM-02 `transmit_claim` and GRSM-03a `relay_group_claim` remain available and are **not retroactively trust-enforced**. Neither old nor new API is exposed as unrestricted player commands. Broader policy consolidation remains a separate gate.
5. **No changes to world truth:** A social tie, feedback, or rumor cannot cause or erase a physical casualty, create resources, change a historical command result, or make every group member aware of a story.

## Persistence and verification

- A **non-destructive SQLite schema v8 → v9 migration** adds `social_tie_meta` and append-only `social_tie_events` (directed pairs, event kind, parent tie event, source knowledge, trust transition, generation/tick and FNV digest chain). Older saves do not gain fictitious preexisting friendships. Immutable database triggers and a unique feedback-per-claim index prevent routine mutation/replay.
- Restore validates contiguous row IDs, metadata count/tip, directed pair continuity, nondecreasing timeline, initial trust, bounded arithmetic, individual knowledge provenance and source/recipient identities. It checks historical co-location against original world snapshots **when retained**.
- **Physical-history caveat:** Only a small rolling set of complete world snapshots is retained by the simulation. After compaction, historical proximity relies on append-only host receipts/digest chains rather than independent reconstruction of a vanished checkpoint. FNV is a noncryptographic corruption detector and not a hostile-admin integrity guarantee.
- **Scale caveat:** Startup still replays and validates tie history. It is not an APS or many-billion-NPC implementation, and repeated snapshot decoding needs bounded certificate/caching work before growing this system.

## Tests

- `tests/GRSM03bTrustTests.cpp`: fresh contact, invalid identities, directional asymmetry, actual physical casualty, confidence monotonicity, positive and negative trust changes, evidence cross-links, duplicate feedback rejection, trust floor, durable restart and copy recovery, historic checkpoint compaction, five forged/deleted trust-row attacks, and a v8 migration fixture that retains original group history.
- Earlier v1/v4/v5/v6/v7 migration tests were updated to **remove v9-only tables** when constructing historical schema fixtures; otherwise their synthetic migrations would not reflect real older databases.
- Targeted Linux native regression: **6/6 passed**, including existing GRSM-01, GRSM-01b, GRSM-02, GRSM-03a, and older general persistence tests.
- Viewer and relay Node.js regression: **84/84 passed**. See `docs/GRSM03b_node_84_of_84.log`.
- Python design unit tests: **3/3 passed**. See `docs/GRSM03b_design_3_of_3.log`.
- Full Linux Debug native/integration regression: **59/59 passed** (38.95 seconds, `-j4`). The first attempted full compilation was interrupted by its execution timeout before CTest started; the resumed full build and CTest completed normally. That initial build interruption is retained separately rather than described as a test failure. See `docs/GRSM03b_ctest_59_of_59.log`.
- Windows-native, human Chromium UI smoke, and actual in-game autonomous conversations remain unverified.

## Next recommended engineering gates

- **GRSM-03c:** Event-sourced joining/leaving and membership epochs, verified member-only relays at time of receipt, voluntary social-contact opportunities, no free intercolony communication, relationship decay at coarse event-driven cadence.
- **GRSM-04:** Distinguish private individual beliefs from *ratified institutional group policy* and add faction conflict, loss/reassignment of leadership, and macro-cohort knowledge without instantly broadcasting to every member.
- **GRSM-05:** NPC/player recognition, separate trust/admiration/fear/grievance/legal standing and actual seven-level information reach; rare optional folklore and dialogue after deterministic evidence and cooldown gates.

## Reproduce on Linux

```bash
cmake -S . -B build-grsm03b -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-grsm03b -j4
ctest --test-dir build-grsm03b -R 'galaxy_(persistence_tests|grsm0)' --output-on-failure
ctest --test-dir build-grsm03b -j4 --output-on-failure
node --test tests/*NodeTests.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

Local development and testing are the source of engineering evidence. GitHub is version-control and documentation, never the required execution/CI host.
