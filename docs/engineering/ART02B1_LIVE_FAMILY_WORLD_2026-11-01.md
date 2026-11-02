# Single Galaxy ART-02b1 — host-only playable-world family integration

**November 1, 2026 — Linux local integration, feature opt-in.** The tested source baseline is ART-02, not the unfinished whole project; GitHub should remain a version-controlled source repository, not a CI development hub.

## Implemented and tested

- **Actual playable-world newborns:** new HostBirth action creates an individual `NpcRecord` with Dependent role at an existing constructed habitat. Each newborn has a stable low-ID, historic birth command, both parent identities, and is added once to the actual habitat dependent count, overall colony total, and needs demand. Named parents must be adults residing in the same habitat. Client commands are rejected; only explicit trusted host APIs may submit birth/death actions.
- **Actual dependent deaths:** HostDependentDeath removes only a born dependent from the active NPC roster, decrements physical housing demand and creates a validated NpcLost physical incident; the family registry retains the deceased person's facial profile, ancestry and immutable event.
- **Durability and recovery:** old snapshot versions v1–v30 remain readable; new v31 serializes dependents and parent IDs. Rejected malformed NPC counts, forged parent fields and missing face/birth receipts. Native operation-journal replay covers submitted host birth/death actions. Restored living dependents are cross-checked with matching SQLite birth and appearance receipts; deceased dependents have physical and family death receipts.
- **One SQLite transaction:** a host birth/death's roster, operation log, world snapshot, family census, genome and immutable receipt share `Store::commit`'s transaction. The appearance store borrows that transaction without committing on its own. Injecting a failure after the snapshot staging rolls the entire unit back; retry succeeds.
- **No magically qualified workers:** born dependents never enter farm jobs, workshops, mentorship, ship crews or population recruitment. They consume existing housing, food, water and oxygen resources; no physical resources are materialized by a birth.
- **No automatic mass birth simulation:** this is intentionally opt-in for individually tracked families. The million-person aggregate PopulationCohorts and cold regional economy require a future reconciled, authoritative bridge. No 3D heads, graphics or NPC age progression implemented.

## Local verification

- New `galaxy_art02b_live_family_tests` PASS: real colony construction/provisioning, adult parent enrollment, trusted birth, idempotent command ID, prohibited player birth, v31 snapshot roundtrip, food/water/oxygen demand, population balance, rollback-injected birth and death, restart, journal-repaired copy, physical/death preservation, and corrupt face receipt rejection.
- Adjacent targeted CTest: **10/10 PASS** (persistence, colony farming, habitation, resilience, policy, workforce, physical incident, ART01, ART02, new ART02b).
- All-target build **interrupted** at 64/176 steps; **do not interpret as full suite pass**. No Windows, renderer, real-life genetics, real-time million-NPC activation, multiplayer warm/cold transition or full sanitizers in this gate.

## Remaining limitations and follow-up gates

- **Integration admission:** trusted host must configure an authorized family policy and enroll resident adult founders, and the family census for a habitat must match the actual adult/dependent occupancy. Existing external macro/census births from ART-02 are not silently promoted. Mismatched scope/seed or population fails closed.
- **Missing age lifecycle:** dependents remain dependents, do not become adult workers; child maturation and per-age food ratios later. Adult/founder mortality currently retains legacy HostCrewLoss, not the family death journal (do not claim full demographic mortality).
- **Housing cap:** current playable colony ship permits 1–1000 named individuals, habitat capacity 8. This is a vertical slice, **not** eight million individually simulated characters.
- **Worker/parent movement:** cross-habitat parental migration, guardianship, aging, adoption, family policy and cold-region birth integration not yet reconciled; do not permit unrestricted transfers that would create census drift.
- **Graphics:** ART-03 procedural heads + ART-04 facial rig still required to test if inherited geometry truly resembles parents.
- **Security:** receipts have consistency hashes/immutable tables but not cryptographic tamper authentication. The host controls authorization and SQLite database access.

## Reproduction

Build with `cmake -S . -B build -G Ninja -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build --target galaxy_art02b_live_family_tests galaxy_npc_family_birth_tests galaxy_npc_appearance_tests galaxy_persistence_tests galaxy_b70n_workforce_tests galaxy_b70f_habitation_tests galaxy_b70i_resilience_tests galaxy_b70m_policy_tests galaxy_grsm01b_physical_tests galaxy_b70e_colony_farms_tests`, then the corresponding `ctest --test-dir build -R ... --output-on-failure`. See `tests/ART02bLiveFamilyIntegrationTests.cpp` for the fixture and deliberate failure injection.
