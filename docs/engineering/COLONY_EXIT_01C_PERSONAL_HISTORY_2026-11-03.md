# COLONY-EXIT-01C — Individual memory and dissatisfaction drive real immigration

**Stack:** CONFLICT-01B3 governance → EXIT-01A real source pressure → EXIT-01B actual cross-owner physical immigration. **No elections, NPC voting or petitions.**

## Personal experience, not colony-wide scripted departure

Each individually named, physically present adult can acquire a durable dated `PersonalOutlook` tied to their own colony, active leader and measured live life-support risk. The score does not assume the player intended any resource shortage. The social part uses only that resident's own **directed GRSM-03c3 rapport toward the then-current leader**, from explicitly host-attested interactions with a genuine recorded same-tick encounter. Group gossip, a neighboring NPC's opinion and unsupported wrongdoing allegations do not count as personal proof. The host-attested interaction system does not itself prove a physically simulated act of harm; game integrations must avoid presenting this as a legal finding.

`record_personal_outlook(event, source, npc)` requires a named resident at their actual home, not travelling, duty-eligible and not the active incumbent leader. Records are spaced by **at least eight actual simulation ticks**, with exactly one appraisal per NPC/source/world generation. Government leadership is read as of its historically effective generation. A source-risk observation is a real material `colony_safety::assess` result, not an imagined worker's emotional state.

## Bounded scoring and hysteresis (initial tunable policy)

Initial dissatisfaction is zero, capped to 0..100. At each approved physical observation, Safe −28, ForecastShortage +8, Critical +34 and Emergency +46. A verified earlier directed leader rapport of −12 or below adds +12, while earlier rapport of +8 or above subtracts 20. Missing relationship evidence is neutral; an NPC does not know another NPC's experiences by magic.

A resident begins **considering departure** at discontent 65 or above, but once active retains that intention until discontent falls strictly below 30. This avoids instant migration from one bad tick and instant forgiveness from one corrected condition. Repeated genuine improvements gradually restore retention. Past history persists through leadership replacements; a new leader does not delete the predecessor's history, and only the new leader's own actual interaction record influences future appraisals.

Only the immutable dated prior event/score can contribute to the next assessment. Social history is read **strictly earlier than the assessment world generation**, preventing later events from rewriting older decisions. No client-supplied mood points, actor-target attribution, timestamps or synthetic NPCs are admitted.

## Physical immigration connection

EXIT-01B `record_voluntary_immigration_admission` now requires a **previously committed, still-active personal departure intention for the exact NPC**, not merely a severe source-shortage report. Its earlier EXIT-01A shortage witness remains necessary as provenance in this first generation of migration mechanics. A source whose water has just recovered can still lose a long-suffering, unhappy resident; after a sequence of genuine good observations reverses the decision, no new admission is possible.

`submit_voluntary_immigration_boarding` rechecks the NPC's latest dated departure intention, present original home and workforce eligibility, current government continuity and receiver safety before physically boarding the exact known ferry. An already-boarded migrant is then governed by the existing real voyage/ART02/ECON03 immigration receipt; mood changes cannot teleport them or silently edit a completed settlement.

Existing receiving-owner acceptance, route, ferry capacity, exact manifest, legal settlement/crew/ART02/ECON03 conservation, startup/recover_copy and atomic rollback stay unchanged. Personal history is appended; there is no separate immigration simulation or mass population editing.

## Durability and historical integrity

New `sg_colony_npc_personal_outlook` records store `world_generation`, `world_hash`, `tick`, source colony, original leader and government epoch, named NPC, referenced earlier directed interaction ID and score, earlier appraisal ID, individual delta, current score and dated `considering_departure` result. SQLite uniqueness, update/delete immutability triggers and generation-CAS transaction admission prevent duplication, forged changes and concurrent past-dated diary entries.

Ordinary startup and independent `recover_copy` run `verify_personal_outlooks` before immigration history verification. For every record the verifier loads the **original pinned physical-world snapshot**, recomputes material risk, then reconstructs historically earlier direct social relationship evidence and prior appraisal. It refuses rewrites to leader, actor, rapport, original risk, elapsed time, score, parent event or intent. Snapshot pruning pins every appraisal generation in addition to earlier pressure and immigration records.

## Native test cases and boundaries

`tests/COLONY_EXIT01B_PhysicalVoluntaryImmigrationTests.cpp` now gives two adults who endure the SAME shortage different valid GRSM interactions: #2 records a negative experience of the leader, #3 records a positive one. After two spaced observations, only #2 considers departure. A foreign colony refuses #3's otherwise real pressure witness; #2 is legally admitted and boards an actual ferry, reaches the new system and settles atomically. The existing migration regression continues to check crew and population conservation, original family/regional custody, durable SQLite replay and refusal of forged ferry/admission history.

`tests/COLONY_EXIT01C_PersonalHistoryTests.cpp` provides an independently resource-backed recovery example: a real colony depletes its onboard water while retaining food/oxygen, a named adult accumulates exit desire, a valid `Action::RecycleWater` converts actual accumulated wastewater back into drinking supply, and three separated safe assessments gradually rescind the intention. Restart, recover_copy, no synthetic casualty, and offline diary tampering are checked.

**Limitations:** This is a bounded observer/decision and admitted travel integration, not an automatic route planner or a probabilistic learned personality. A trusted-host scheduler must call appraisal methods on actual world ticks and authenticate any receiving-player acceptance. No binding election, voting or petition, no legal guilt inferred from physical scarcity, no adult-with-dependents or multi-parent voluntary move in the narrow EXIT01B route. The existing ART02 household route must be reused for family travel. An existing pre-EXIT01C admission compatibility check should be reviewed before promoting draft stages to v1.0 release.

Focused CI `.github/workflows/colony-exit01c.yml` checks personal memory/hysteresis and end-to-end immigrations plus historical v59 family and GRSM tests. Windows hardware acceptance and broader review stay separate.
