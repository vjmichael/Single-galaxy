# GRSM-02 — NPC Evidence, Testimony and Conflicting Accounts

**Single Galaxy | 2026-10-31 | Local Linux engineering gate**  
**Status: implemented and tested as a host-controlled individual-NPC knowledge vertical slice.**  
**Not implemented: autonomous rumor propagation, notoriety tiers, factions, player identification, dynamic dialogue, TTS, galaxy-scale APS.**

## Purpose

Distinguish three things which must never be collapsed into a single database notion of truth:

1. **Physical history:** GRSM-01/01b's immutable commands, wrecks, resource transfers, NPC losses and journeys; these are world-authoritative.
2. **Locally grounded observation:** a surviving NPC has a demonstrable *opportunity* to observe a freshly committed physical incident in their location or within a small proximity range. Being local does **not** imply guaranteed accurate sight, identity recognition, or correct interpretation.
3. **Transmitted claim:** a recipient may accept and later repeat an account, including a favorable, unfavorable, exaggerated or contrary version. That text and sentiment are *testimony*, never a physical world mutation.

## Implemented design

- SQLite **user_version 7**, migrating non-destructively from v6. Old incidents stay intact; eyewitnesses and rumors are **not retroactively invented**. A `knowledge_meta` boundary prevents a freshly upgraded save from treating an old incident on the same tick as newly witnessed.
- Append-only `npc_knowledge`: stable sequential record IDs, incident reference, parent knowledge record, speaker and recipient NPC IDs, account text, classification (`LocalObservation`, `Faithful`, `Embellished`, `Contrary`), sentiment [-100,100], confidence [0,100], observed generation/tick, and a linked FNV digest. Database triggers prevent ordinary mutation/deletion. The digest/chain detects ordinary corruption and missing final leaf records but **is not a cryptographic security boundary**.
- API `record_local_observation(incident_id,npc_id)`: host-only call after a fresh committed incident; NPC must still be alive and near the relevant physical event. Death witnesses must be aboard the affected colony ship; cargo witnesses must occupy a named source or destination entity; wreck witnesses must be within 5,000 in-simulation millimeters of the surviving wreck and in the same system. One firsthand record per incident/person. Journey eyewitnesses and line-of-sight sensing are deferred.
- API `transmit_claim(parent_id,listener,classification,valence,confidence,account)`: parent recipient is the only eligible speaker; speaker and listener must be surviving, distinct NPCs currently at the same location; account is 1–240 bytes. A transmission retains the incident and parent provenance, but is **not validated as historically accurate language**. The same listener can hold mutually contradictory accounts from different people.
- API `recent_knowledge(limit)`: read-only, 1..256 rows, newest-first; does not scan millions of records for an ordinary UI read. Full validation still scans knowledge history during startup in this prototype.
- Every observation or transmission commits in its **own SQLite transaction**, after the authoritative world effect. It does not mutate the world snapshot or create/destroy goods or people. Stale world generations fail closed. Future automatic witness capture may move event generation and observation into a single worker-controlled checkpoint.
- On restore, check complete row sequence, migration boundary, parent chain, recipient-to-speaker identity, incident existence and time, field bounds, digest chain, and row count/tip. Missing, edited or impossible provenance is rejected rather than reinterpreted as physical truth.

## Local tests

- Native `galaxy_grsm02_knowledge_tests`, **3/3 scenarios**:
  - 24-person original colony plus a distinct distant colony, death of one named resident; survivors locally testify; dead NPC, remote NPC and unknown incident rejected. A faithful account is passed to one NPC, then an embellished version to another, while a contradictory account from the original witness reaches that same listener. Both accounts persist through restart and recover-copy; no changes to NPC headcount or physical event.
  - Cargo-transfer evidence aboard an inhabited colony ship; nearby observation of a persistent wreck; a distant NPC cannot witness either event.
  - SQLite v6 to v7 migration retains losses but does not invent retroactive witness claims; later physical incidents become witness-eligible. Altered, missing or wrongly sourced records fail startup validation.
- Full Linux native/integration regression: **57/57 CTest tests passed**.
- Viewer/relay JavaScript regression: **84/84 Node.js tests passed**.
- Run: `cmake -S . -B build-grsm02 -G Ninja && cmake --build build-grsm02 -j8 && ctest --test-dir build-grsm02 -j8 --output-on-failure && node --test tests/*NodeTests.js`.

## Boundaries / next gates

1. This is **proximity-attested opportunity to observe**, not a sensor model or proof the NPC actually saw or understood the incident; no exhaustive witness discovery, relationship memory, faction beliefs, or player-attached reputation yet. In this narrow gate the developer/host deliberately invokes evidence registration.
2. `ClaimFidelity` records the **stated narrative type**, not independently verified truth. The renderer does not generate language with an AI; text is provided by the trusted test host. A fabricated claim still references a real underlying incident; wholly invented incidents are deferred and must never enter the true history ledger.
3. Co-location for conversation is represented by an identical current `NpcRecord.location`, not detailed distance/sound propagation. Cross-colony information transfer is not automatic and should later require legitimate communication infrastructure.
4. Per-save restore uses an **O(number of knowledge records)** integrity scan and retains records in-process during validation; global APS population-scale use will need incremental receipts, cold storage, materialized cohort/faction summaries and selective replay.
5. Physically consequential `HostDestroyShip` and `HostCrewLoss` remain host/test-only. This milestone does not implement damage, player hostility, crime attribution, passenger manifests or transport AI.
6. Next recommended gate: **GRSM-03** — faction/group membership and shared interpretations, with a verified knowledge-propagation topology; start with a small real workforce rather than universal instantaneous galactic news. Follow with recognition, incident salience, rarity/cooldowns, and optional dialogue presentation.

## Engineering workflow

Development and testing occurred locally. GitHub `vjmichael/Single-galaxy` is the source/provenance mirror; `/Single Galaxy` ChatGPT Library retains milestone handoff archives. It is not a persistent CI-controlled development hub.
