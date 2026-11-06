# DISPOSITION-02 — Territorial Claims, Invitations, Warnings and Trespass History

**2026-11-06 status: source committed; native Windows/Linux acceptance pending.**

This branch extends **DISPOSITION-01** instead of replacing colony personality, CONFLICT-01 governance, reputation, or the original NPC flight ledger.

## Authoritative evidence and finite geography

`TerritoryJournal` stores a bounded **local** territorial claim anchored to the real position of an existing colony ship when the claim is recorded. The claim is a stationary, bounded disk of at most **5000 mm** in the game's local frame; it is not a generated planetary ownership grid, hyperlane sovereign border or globally enforced treaty. The host must authenticate the claimed colony's actual owner, and the simulation is the source of position, system and local-frame identity. The journal uses an SQLite table in the **same world database**, with its own append-only history and original world tick/generation witnesses. It does not alter the simulation's NPCs, cargo, planetary generation, player character or material ledger.

The trusted host is responsible for authenticating the caller identity and serializing jurisdiction operations against the original PersistentWorld. Merely passing a numerical `PlayerId` is **not** an authentication token.

## Event history

Each event has a unique ID, monotonic sequence, recorded tick and generation, original owner/colony, event type and referenced parent. An append-only SQLite table has UPDATE/DELETE guards and a deterministic chained digest. Every history read checks the chain, event ordering and whether warning, trespass and expulsion records cite their correct original claim/warning/trespass parent with matching owner and physical visitor. This is **tamper evidence**, not a cryptographic signature against an attacker who can rewrite the database and recompute every digest. Production protected evidence/authenticated signer separation is a separate task.

| Native event | Preconditions | Result |
| --- | --- | --- |
| Claim | Valid owned real colony ship, finite radius, no duplicate claim | Frozen local region |
| Invite | Original claim, authenticated colony owner, distinct foreign owner | Visitor is invited within that region |
| Revoke invitation | Actual previous active invitation | A fresh uninvited visit begins; historical incidents persist |
| Warning | Actual foreign ship inside the original region, real currently on-duty NPC witness, not invited, 2–10000 tick grace | Recorded notice, no crime or attack |
| Trespass | The exact warned ship remains physically inside after grace, is not invited, fresh real guard observation | Recorded boundary incident, no automatic conviction |
| Expulsion | Actual witnessed recorded trespass in the region | Effective excluded status |
| Re-invitation | New explicit owner action | Lifts **active** exclusion and permits a fresh grace period on later revocation without deleting history |

Cross-frame or outside-region contact, wrong owner, invitation, premature grace completion and repeated records fail closed. An NPC cannot observe foreign ships outside the original bounded `ColonyDefense` watch range.

The domain is intentionally limited to historical *territorial standing*. The current `ColonyThreatPolicy` explanation and `ColonyTerritoryResponse` overlay can produce warning, hailing or interception intentions from verified recorded standing. It cannot issue a criminal finding, arrest warrant, damage permission, cargo seizure or unlawful player custody transition.

## Original response integration

`ColonyTerritoryResponse.hpp` combines the real `TerritoryJournal::standing` with `DISPOSITION-01::assess`, which already uses the original CONFLICT-01 previous-generation GRSM, effective real NPC leader and actual shortages. It does **not** invent visitor invitations or hostile crimes from a diplomatic score.

`dispatch_territorial_patrol` requires real inside-region standing, an existing warning or trespass, a fresh named-NPC original observation, and then delegates to the original `NPC-RESP-01::dispatch_observed_patrol`. That uses the same qualified NPC pilot, original flight order and native physical rendezvous; there is no second navigation engine.

## Hostile rollback and recovery

The journal's `BEGIN IMMEDIATE` protects append ordering across writers. A test-only `TerritoryFailPoint::AfterInsert` injects failure before COMMIT, proving transaction rollback and that a second SQLite connection sees no partially published claim or warning. The recovery test independently reopens the actual world and territory journal, checks frozen history, original ship and material quantities, and rejects SQLite UPDATE/DELETE attempts and altered historical claim radii. Native Windows and Linux CI builds this fixture together with previous governance and disposition regressions.

**Still outstanding:** authoritative large-scale planet/system territorial geography, invitation signatures/treaties, electronic transmission/notice delivery to the player (the native guard currently witnesses the foreign ship, but no UI notice transmission receipt is proven), explicit appeal/contest policy, and independent lawful or criminal combat and custody authorization. Do not mark these finished from DISPOSITION-02.
