# PLAY-10 — Native Persistent Exploration and Discovery Journal
**November 4, 2026 · Single Galaxy · branch `integration/play10-persistent-exploration-20261104`**

## Why this work
The original colony `SCAN` path presented a broad, immediate geological list but had no journaled first-discovery event. The separate B6.1 survey archives/probes support older non-colony gameplay; showing them as if they were persisted colony observations would be misleading. PLAY-10 adds a small **native first-discovery ledger** to the same authoritative SQLite-backed simulation.

## Mechanics
- The player chooses **Scan nearby space** (also the original Scan asteroids button). The authenticated relay resolves the owning player and sends the existing `SCAN` command. The colony host now submits a new native `Action::SurveyLocal` at the next committed simulation tick.
- The native ship must be controlled by the issuer, in open space, and ready (not jumping). The scan searches current, real asteroid entities within **50,000 mm**, same system only.
- Each newly found site produces a durable immutable record: player ID, source entity ID, system ID, true native coordinates, first-discovery tick and whether the asteroid is geological. Repeat scans **never overwrite the first-observed tick or fabricate another discovery**.
- This new collection serializes as `discovery` rows in canonical **snapshot v63**, is included in state hashes, validates IDs, coordinates, owner/timestamp, bounded record count, and survives SQLite restart/recovery. Historical first observations remain after an asteroid is depleted, because discovery history differs from current world resources.
- The host emits strict `COLONY|1|tick|DISCOVERY|id|system|x|y|first_tick|geological` notifications from the native ledger to the owner; it sends current rock/mineral readings only after **successful committed scans**. Login does **not** automatically scan unexplored space.
- The exploration HUD is observational. It displays native saved records and allows a player-triggered local scan; no localStorage, duplicated SQLite, client-generated locations, fabricated hidden sites or browser-side inventory/world modification.

## Design boundaries
The saved ledger is a **first-discovery archive**, not a full 3D mapping subsystem or a proof of resource quantities today. Existing SGVIEW live projection still shows rendered nearby world entities as a debugging visualization; they are not credited as scanned discoveries. Ship-first opening and optional tutorial remain unchanged. New galaxy regions, NPC-hostile discovery AI, commercial survey publication and collaborative maps should build on this ledger later without creating a second source of truth.

PLAY-08's current 12-shot practice weapon remains temporary; as instructed, full ammunition logistics will be designed later with the complete weapon system, not in this exploration phase.

## Verification gates
- `tests/PLAY10_ExplorationPersistenceTests.cpp`: real C++ native survey, owner rejection, local range, no repeat count, physical travel to a remote deposit, historical records after mining/depletion, SQLite restart and recovered copy.
- `tests/ExplorationJournalTests.js`: deterministic first-observation ordering and display of verified records.
- `tests/PLAY10_ExplorationWiringTests.js`: strict COLONY telemetry parsing and authenticated native-scan/relay UI contract.
- `.github/workflows/play10.yml`: builds native game host and runs these checks with preceding UI regressions.

**Outstanding:** exact-current-revision GitHub CI, real Chromium/Windows playthrough, tick-latency and telemetry performance with large exploration journals, source-discovery privacy of SGVIEW visible-world projections. Leave draft until these are independently verified.
