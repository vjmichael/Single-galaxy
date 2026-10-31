# Single Galaxy

**Original offline-capable space exploration, colony, industry, society and emergent-civilization game (early Linux prototype).**

In the released game's intended opening, one player colony ship with finite NPC crew enters an unexplored fictional galaxy. No stations, gates, populated settlements, free factories or markets are preplaced. The player and later NPC communities develop infrastructure and history through physical work, supplies, travel and decisions.

## Current prototype status

This repository begins from the **B7.0n** source/testing archive, dated 2026-10-31. The archived evidence records a Linux pass of 54/54 CTest native/integration suites and 84/84 Node tests. The opt-in individual workforce model persists up to 24 original NPC crew identities and farm work/mentoring; a separate statistical cohort prototype demonstrates bounded large-number accounting. **Those are not a live trillion-NPC universe.** The game remains a bounded 2D colony/space prototype, not a continuous 3D multiplayer galaxy. Native Windows and full interactive Chromium playthrough remain open gates.

## Design documentation

- [Galactic Reputation & Social Memory (GRSM)](docs/design/Galactic_Reputation_and_Social_Memory_v0.1.md): canonical deeds versus conflicting NPC/faction beliefs; seven notoriety reach levels; heroism, crime, evidence, recognition, rare folklore.
- [Adaptive Population Simulation and Factions](docs/design/Adaptive_Population_Simulation_and_Factions_v0.1.md): macro/micro layering, cohort distributions, personalities, overlapping social groups.
- [Decisions register](docs/design/Core_Decisions_2026-10-31.md): game continuity, governance, migration, society and open design choices.
- [Engineering next gates](docs/engineering/NEXT_GATES_2026-10-31.md): verified current capabilities versus proposed code.
- [B7.0n archived source README](docs/engineering/README_B7_0n_archived.md): earlier milestone-by-milestone project overview and run commands.

## Build and test (Linux)

Requires the toolchain/dependencies described in the archived project documentation (C++/CMake, Ninja where used, Node for viewer tests, SQLite dependency as appropriate to environment).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build -j4 --output-on-failure
node --test tests/*.js
python3 -m unittest discover -s tests/design -p 'test_*.py'
```

Tests and development are local-first. This repository is for version control, code provenance, milestone history and documentation; it is not intended to make feature progress depend on CI availability.

## Status warning

GRSM, automatic personality-driven faction formation, multi-colony migration, true persistent passenger traffic, voice rumors, galaxy-scale APS and full autonomous colony governance are **design proposals**, not implemented systems. See milestone reports for actual feature evidence.

## License / distribution

**License and bundled asset provenance are pending review.** No open-source license is granted by this repository document. Keep a new hosted repository private until the user chooses a distribution policy and source/third-party provenance is audited.

## GRSM-01 · Verified historical-event foundation (2026-10-31)

The first Galactic Reputation & Social Memory engineering gate adds an append-only, SQLite v5 ledger of **executed command outcomes**, written atomically with the physical world state and validated against snapshots, receipts and journal on restart. It is **not** a rumor or social-reputation engine. See [`docs/GRSM01_Durable_Historical_Events_Report_2026-10-31.md`](docs/GRSM01_Durable_Historical_Events_Report_2026-10-31.md) for boundaries, migration and test evidence. Local Linux regression: 55/55 CTest and 84/84 Node tests.

## GRSM-01b · Physical incident ledger (2026-10-31)

SQLite schema v6 and kernel snapshot v21 now record exact physical movements, completed journeys, persistent wrecks, and named ship-crew casualties. Ship destruction and casualties are **host-only test transitions**, not implemented player combat; no commercial passenger transport AI or reputation decisions are claimed. Read [`docs/GRSM01b_Physical_Incidents_Report_2026-10-31.md`](docs/GRSM01b_Physical_Incidents_Report_2026-10-31.md). Local Linux checks: 56/56 CTest native/integration and 84/84 Node tests.

## GRSM-02 · NPC evidence and conflicting testimony (2026-10-31)

SQLite schema v7 adds host-attested local observation and provenance-linked NPC-to-NPC testimony **without changing physical truth**. A surviving NPC can receive faithful, embellished, or contrary stories, and contradictory accounts remain separately recorded. Direct knowledge requires a new nearby incident; migration never invents retroactive witnesses. No automatic gossip, faction standings, notoriety levels, LLM-generated chatter, or speech AI is claimed. See [`docs/GRSM02_Evidence_Claims_Provenance_Report_2026-10-31.md`](docs/GRSM02_Evidence_Claims_Provenance_Report_2026-10-31.md). Local Linux regression: 57/57 CTest, 84/84 Node.js.

## GRSM-03a · Local social groups and witnessed relays (2026-10-31)

SQLite schema v8 adds overlapping groups founded from existing, co-located workforce identities; only real member-to-member local contact can add an individual group-context claim. Each group can retain multiple *interpretations* of one incident, with provenance to the claim that prompted it; absent members receive no automatic knowledge. This is a small host-controlled gate, **not** automatic gossip, a faction consensus mechanism, universal recognition, or an intercolonial media network. See [`docs/GRSM03_Social_Groups_Relays_Report_2026-10-31.md`](docs/GRSM03_Social_Groups_Relays_Report_2026-10-31.md) for testing and integrity limitations.
