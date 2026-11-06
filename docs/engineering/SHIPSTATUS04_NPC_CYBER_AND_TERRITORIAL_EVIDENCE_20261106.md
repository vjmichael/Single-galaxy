# SHIP-STATUS-04 — NPC Cyber Operations and Territorial Attack Evidence

**Engineering state (2026-11-06):** native implementation, original-world operator attribution, regression source and Windows/Linux test matrix committed in a stacked draft. Exact-head native CI and installed gameplay acceptance have **not yet passed**.

## Scope and boundaries

The project reuses SHIP-STATUS-01's temporary Confusion, SHIP-STATUS-02's actual module-state executor and atomic SQLite state, and SHIP-STATUS-03's physically fitted intrusion emitter, target/range/cooldown and firewall/isolation. **No independent NPC gun, hacking effect, ship-control, inventory or combat engine is introduced.**

A colony **may choose to commit an aggressive or unlawful cyber attack**. A watched contact, ignored hail, trespass claim, strategic hostility or patrol movement is never *by itself* a criminal conviction or license to injure or detain the target. Original gameplay must keep the difference between an actor's ability to attack, its political motive and the jurisdiction's legal judgment.

### Original governed tactical trigger

`NpcCyberTactics.hpp::dispatch_governed_hack` now consumes the **original** `ColonyDispositionCoordinator::assess` and demands `ColonyConflict::Posture::PrepareRaid` from a real dated colony-watch contact and actual effective original leader/governance policy. It **records the original strategic decision** via `record_colony_diplomatic_intent` before submitting the same physically verified NPC command. A peaceful, warning-only or unprepared colony has no automatic hacking action. A rejected or out-of-range physical command never gets magically upgraded to a successful attack merely because its leader selected PrepareRaid.

`ColonyConflict::evaluate` also recognizes a real installed and **powered** intrusion emitter as physical attack preparation, in addition to the existing stock-based capacity threshold. A colony with no hardware and no original stock still cannot magically prepare a raid. Neither strategic intent nor a successful attack implies legal justification or a criminal verdict.

## Native NPC operator

`PersistentWorld::submit_npc_cyber_attack(owner,attacking_ship,pilot_npc,observer_npc,target,command_id,fail)`:
- trusted host binds `owner` to the actual original colony authority / deliberate AI tactical decision, never an untrusted client owner number;
- source is a real, commissioned original colony ship with installed intrusion emitter and the original finite crew;
- named shipboard pilot is present, eligible and not already assigned to a conflicting task; named watch observer is also physically aboard and duty-eligible;
- fresh original `ColonyDefense::observe_local_contacts` confirms the real foreign ship at the current world tick; target IDs alone do not establish observation;
- an active original NPC flight (Planned/InTransit) is exclusive, so cyber tasks cannot seize a navigator's ship;
- validates the original command ID namespace and schedules **the same** `Action::HackConfuse` used by the player at tick+1;
- the original attack executor independently checks module ON state, physical proximity, target readiness, cooldown, ownership and effective countermeasures at execution. If a target moves away, the command fails normally, not teleports the target back.

There is no instant NPC action from a high-level disposition/territory read model. This is a host-authorized operation, so a criminal colony can still attack if its actual policy decides to do so, but an AI dispatcher must make that decision explicitly.

## Canonical v102 operator receipts

The SHIP-STATUS-02/03 original `Simulation` now retains `NpcCyberOperator` keyed by the **original command ID**, with genuine owner, attack ship, target, named pilot, named observer, and posting tick. The old command and new attribution are committed **in one** `PersistentWorld::Store::commit` SQLite generation. This is not a parallel NPC ledger.

`galaxy-kernel-v102` adds deterministic `npccyber|...` records to the original canonical snapshot. Restore validates no duplicate/invalid operator receipts, source owner/target identity, original accepted command ID, and the presence of the exact pending command or executed native event. The original `Simulation::swap` publishes both maps only after durable COMMIT. Older v1–v101 snapshots remain readable.

Before the NPC-attributed command is executed, the native simulation rechecks whether its registered operator is still a real, available original shipboard pilot. A casualty, reassignment or transfer can cause `Unauthorized`, not an inexplicable ghost action.

This audit is an operator identity / observed-contact receipt; it does not assert that a pilot's later decisions or a legal verdict are retroactively proven by a name alone. Future NPC tactical planning should feed an original durable governance/host authorization record into this entry point.

## Territorial cyber evidence, NOT automatic crime

`TerritorialCyberEvidence.hpp` joins:
1. a current-tick original `HistoricalEvent` for an actual `Action::HackConfuse` attempt with applied effect or physically resisted emission;
2. source and target genuine physical ships and their original owners;
3. an existing local territorial claim covering the foreign attacking ship and the attacked colony (or another independently covered foreign victim);
4. a current original named colony guard observation of the attacker, at the **same tick** as the original attack.

The result is `ObservedCyberAttack`. It always has `legal_crime_finding=false`, and grants **no** arrest, damage, seizure, custody or 1.5× aggravation. A future jurisdiction/legal fact finder must decide whether the actual cyber incident violates colony law and pass a separate verified offense into the existing `territorial_hail_aggravation` interface. Never multiply a mere territorial allegation.

Because the read model intentionally refuses a later, unobserved backdated claim, the host must persist any observed incident into the future legal-case journal during the same tick. This increment does not create a fake law database.

## Native acceptance

`SHIPSTATUS04_NpcCyberTacticsTests.cpp` constructs **two real colony ships and finite NPC crews**, provides life support, a real scanner and a finite physical ore deposit, then uses Mine→RefineOre→FabricateModule→ConfigureCyberModule. The test verifies:
- wrong owner, fake pilot, wrong observer, same-ship target and out-of-sight foreign ship denial;
- no free status effect or material from rejected admissions;
- failpoint `AfterSnapshotWrite` rolls back both world and NPC operator receipt;
- accepted original NPC command is unique, with v102 operator/observer attribution durable before execution;
- independent SQLite restart and actual original HackConfuse execution applies Confusion and denies the native scanner;
- a correctly located, duty-eligible defender guard observes current-tick cyber evidence in its claimed region without granting a legal verdict;
- a later tick cannot fabricate retrospective eyewitness evidence;
- independent `recover_copy` preserves the original effect, operator identity, resources, and expiry.

Windows/Linux CI includes SHIP-STATUS-01/02/03/04 plus PLAY-10 exploration in the existing status workflow.

## Outstanding next gates

- Exact-head compilation and behavioral acceptance across Windows/Linux, repair failures.
- Extend the now-wired original `PrepareRaid` strategic trigger into a recurring host-supervised NPC tactical cadence with cooldown, squad decisions, equipment reserves, player-visible notification and disengagement. The present command dispatcher is not a background AI combat loop.
- User-facing module/effective-state diagnostics and accessible temporary HUD interference.
- Once the separate original criminal law-case mechanism has actual evidence and rules, link the observed cyber attack to a legal verdict and *then* optional hail-aggravation recommendation. No fake conviction, custody or physical interdiction.
- Installed game acceptance with real NPC movements and resource constraints.
