# GRSM-03c — Event-Sourced Group Membership and Receipt-Time Eligibility

**Single Galaxy | 2026-10-31 | local Linux milestone**

## Engineering objective

Turn GRSM-03a's static founding rosters into bounded, durable **living memberships**, preserving GRSM-02's distinction between physical fact and transmitted claim. This is the membership/receipt half of the GRSM-03c program, not a complete autonomous social ecology.

## Implemented

- **SQLite schema v10:** Non-destructive upgrade from v9, new `membership_meta` and append-only `group_membership_events`, immutable row triggers and digest-chain tips. Pre-v10 group relays acquire a zero `membership_seq` (their actual founding membership); migrations do not fabricate joining, leaving or old witnesses.
- **Group membership events:** A survivor may leave a group that currently includes them, or join/rejoin with sponsorship by an active, co-located member. Invalid, duplicate, remote or dead-person transitions are rejected. A group is bounded to 24 active members. Founders are not overwritten or erased; every transition advances a per-group epoch and records its prior event, world generation and tick. Multiple overlapping memberships are supported.
- **Host-controlled access:** `change_group_membership`, `active_social_group_members`, and `recent_group_membership` are local host/test APIs; they are not player-issued administrative commands. Social group list results show current membership, while the original founder roster persists immutably.
- **Receipt-time member-only testimony:** New group relays capture a global monotonic membership-event sequence at the same `BEGIN IMMEDIATE` transaction where the individual claim and group-relay receipt are inserted. Both speaker and listener are checked against that specific epoch. A subsequent leave/rejoin does not rewrite past eligibility or retroactively teach an absent NPC.
- **Interpretation guard:** Group opinions recheck currently active speaker/recipient membership *inside* the write transaction. A departed NPC cannot use the narrow group-view API to issue a new interpretation based solely on past membership.
- **Restore:** Replays ordered membership events and verifies per-group epoch/parent links, social membership limits, real member sponsorship and co-location wherever a historical full snapshot survives, chain tip/count, and member status at the actual recorded relay epoch. The migration boundary and source-recovery copy retain original physical casualties and contradictory narratives.

### Explicit boundaries

1. **No automatic gossip or faction AI.** A membership change does not send a story to the joining or remaining members; an absent member learns nothing. Host-driven group relays still require physical co-location. The separate GRSM-02 and trust-gated individual testimony APIs remain distinct and are not forcibly folded into group policy.
2. **No magical intercolony communication.** A distant NPC cannot join from another ship without actually meeting a group member. No comm-network topology, NPC transport AI, or scheduled contact meetings have been introduced yet.
3. **No automatic social decay or political leadership.** Coarse opportunity cadence, trust/relationship aging, elected officers, ratified institutional beliefs, and group split/merge remain additional gates. Leave is voluntary for existing, living members; death does not autonomously dissolve affiliations.
4. **Bounded prototype:** Up to 24 active members per group, with linear historical replay. Retained full-world checkpoints are a rolling window. After checkpoint compaction, older host-attested contact is checked against immutable receipt history rather than a full surviving physical snapshot. The chained FNV digest is an accidental-corruption detector, not adversarial cryptographic authentication.
5. **Historical opinion precision:** Group relays record exact membership sequence; group views are guarded during recording but pre-v10 view receipts do not independently encode a second membership sequence. Upgrade that precision before allowing remote ratification, delayed view recording, or complex multi-actor governance.
6. **No claim of Windows/Chromium verification** or of a production-scale NPC society. Tests run locally in Linux.

## Verification record

- Targeted new/legacy suite: **5/5 passed** (`galaxy_persistence_tests`, `galaxy_grsm02_knowledge_tests`, `galaxy_grsm03_social_tests`, `galaxy_grsm03b_trust_tests`, `galaxy_grsm03c_membership_tests`).
- New C++ test: invalid sponsor, remote join, duplicate join/leave, dead NPC, real member join/leave/rejoin, member-only claim relay after departure, competing group views, physical casualty unchanged, save/recover-copy and restart, 5 deliberate table tamper/deletion attacks, true v9 fixture migration.
- Linux **full native build succeeded**. A first complete pre-guard CTest run passed **60/60**. The subsequent post-guard complete attempts were **59/60 (-j4)**, **59/60 (-j2)**, and **58/60 (serial)** because of varying older live-relay failures. **The final full-suite gate is not green**; all 60 individual tests did pass when including later isolated reruns, but that is not a substitute for one green complete run.
- JavaScript viewer/relay: **84/84 passed**.
- Python design contract tests: **3/3 passed**.
- The first broad build was interrupted by the execution window, then resumed incrementally to completion. A pre-guard full run passed **60/60**. After the extra group-view transaction-time guard, the `-j4` run failed **B6.2c destination-state relay**, `-j2` failed **B7.0b relay startup**, and the serial run failed **B7.0c mineral** and **B7.0d agriculture relay startup**. All four were rerun independently and passed. None of these tests or the relay implementation was modified by GRSM-03c. Their exact causes are unproven; failures and successful isolated reruns are preserved in the archive, not silently discarded. Make deterministic relay-readiness and scheduling tests a distinct infrastructure QA repair gate.

### Reproduce

```sh
cmake -S . -B build-grsm03c -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-grsm03c -j4
ctest --test-dir build-grsm03c -j4 --output-on-failure
node --test tests/*NodeTests.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

## Next recommended steps

- **GRSM-03c.2:** Host-supervised, event-driven recurring meeting opportunities with bounded work; optional social contact freshness or relationship aging that does not impose automatic global forgiveness; strictly physical travel or explicit communications infrastructure for multi-colony routes.
- **GRSM-04:** Individual beliefs versus formally ratified organizational policy; leadership vacancies, factions competing over evidence, group split/merge and bounded APS cohort roll-ups.
- **GRSM-05:** Separate notoriety reach, recognition, fear, trust, admiration, grievance, and lawful standing; rare optional folklore, not constant chatter.

Local source build and tests are the engineering basis. GitHub is the history/provenance repository only, not a prerequisite for development.
