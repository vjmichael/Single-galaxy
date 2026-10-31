# GRSM-01b — Exact Physical Incidents and Loss Accounting

**Single Galaxy · 2026-10-31 · Local Linux engineering gate**  
**Status: implemented in C++/SQLite test kernel; not a player combat, NPC traffic, or reputation feature.**

## Purpose

Create an authoritative, recoverable history of *physical consequences*, distinct from fame, notoriety, hearsay and faction beliefs. A story cannot kill an NPC, destroy a ship, transfer cargo, or teleport a traveler. Only a committed world-state transition can generate an authoritative incident.

## Implemented

- SQLite schema **v6**, with immutable `physical_incidents` records and `physical_meta` migration boundary. A v5 save starts recording **new** physical incidents at upgrade; historical events before the boundary are **not backfilled or invented**.
- Typed read-only API `PersistentWorld::recent_physical_incidents(limit)` (bounded 1–256) for `CargoMovement`, `JourneyStarted`, `JourneyCompleted`, `ShipDestroyed`, `NpcLost`.
- **Cargo movement:** each new source→destination resource-ledger entry creates an exactly corresponding physical movement record with quantity, reason, timestamp, command ID when present, and durable resource-ledger identity. Includes commanded and autonomous resource transfers.
- **Journey:** a successful gate transfer starts one recorded journey with ship, gate, origin/destination systems and real cargo at departure. The subsequent physical arrival creates a distinct completion incident only when the jump finishes. No completed trip is inferred from a pending departure. Existing v5+ `jump_ledger` and snapshot activity remain the authority.
- **Ship destruction vertical slice:** a **server/test-only** `HostDestroyShip` command converts an eligible ordinary ship in space into a persistent `Wreck` entity. Its cargo, position, system, former owner and identity are preserved; ore mass is conserved. The wreck is not a flyable ship. Player-submitted copies are rejected. This is a host-determined consequence test, **not yet a damage, weapons, shield, salvage or combat mechanic**. Populated colony ships are deliberately excluded from this first wreck transition.
- **Named NPC casualties vertical slice:** a **server/test-only** `HostCrewLoss` command permanently removes the selected number of existing, ship-located individual `NpcRecord`s, reduces the corresponding physical ship headcount and records one `NpcLost` event **per deceased identity**. The next-ID counter is not rewound. The last surviving crew member cannot be removed in this slice, respecting the current positive-crew snapshot invariant. This is a deterministic casualty-conservation test, **not yet an injury, natural death or passenger transport system**.
- All typed incidents commit in the *same* SQLite transaction as the world checkpoint, command receipts, historical event ledger and original resource/jump ledgers. Failpoints roll everything back without publishing partial harm.
- On reopening the SQLite world, all post-boundary physical incident rows are reconciled with **independent** physical resource/journey ledgers, the command journal, and the saved simulation's activity events. Missing, altered or extra incidents fail closed. Ordinary SQL UPDATE and DELETE are blocked by triggers; the simple FNV checksum is accidental-corruption detection, not cryptographic authentication.
- Snapshot format **v21** extends the existing v20 format to support wrecks and physical-loss activity without reassigning old action/type codes. A ship's *completed historical journey* remains valid after its later destruction.
- Historical GRSM-01 `historical_events` remains the authoritative list of all executed command outcomes; GRSM-01b incidents represent **only validated physical effects** of those outcomes, not moral judgments.

## Local test evidence

- `cmake -S . -B build-grsm01b -G Ninja` and `cmake --build build-grsm01b -j8` completed on Linux.
- **56/56 CTest native/integration suites passed** (`ctest --test-dir build-grsm01b -j8 --output-on-failure`).
- **84/84 Node.js tests passed** (`node --test tests/*NodeTests.js`).
- `galaxy_grsm01b_physical_tests` exercises:
  1. **Real ore transfer → durable cargo fact**; ship traverses a gate, gets departure and arrival records; another ship follows and destroys it after arrival; wreck keeps its owner and cargo; both trips survive recovery.
  2. **24-identity colony crew → three permanent identity losses** with headcount 21; a separate uncrewed ship's cargo-bearing wreck survives save/restart and journal recovery. User-authored host command is rejected, failpoint cannot publish a destruction, and altered/removed incident rows fail validation.
  3. **v5 → v6** transactional migration boundary: old executed cargo movements are *not* fabricated into new events; post-upgrade transfer produces exactly one new fact.
- Existing persistence, gate, colony, player authorization, workflow, client projection and relay test suites remained green.

## Scope limitations and next gates

1. **No combat or commercial transport AI.** Wreck and casualty actions exist only as test/host-supplied physical outcomes. A future attack/damage engine must validate causes, defenses, jurisdiction, passengers, injuries, and salvage before exposing destruction to players.
2. **No claim that people die aboard ordinary cargo ships.** The tested ordinary ship is uncrewed; casualty mechanics currently operate only on individually tracked *colony-ship* residents. Passenger manifests and ship destruction with occupied vessels require a dedicated later gate, including survivors and evacuation, crew/family/workforce effects, cargo salvage, and player accountability.
3. **No rumor, reputation or witnesses yet.** GRSM-02 should layer evidence provenance, discovered vs unknown incidents, conflicting faction beliefs, recognition, and misinformation over *immutable* GRSM-01/01b truth.
4. **Scale and historical retention:** snapshots still contain all simulation activity for this prototype; persisted SQLite facts are validated by full scan on recovery. A multi-billion-person society will need incremental proofs, bounded loading, and cold-history storage.
5. **No guaranteed permanent tool workspace.** Code is built/tested locally in the current environment and archived in the `/Single Galaxy` ChatGPT Library; the repository is version history and a backup, not a replacement for a local checkout on the developer's computer.

## Repository workflow

Local development → reproducible tests → write evidence → commit → mirror to `vjmichael/Single-galaxy` only after verification. No GitHub Actions dependencies or Cloud-first development.
