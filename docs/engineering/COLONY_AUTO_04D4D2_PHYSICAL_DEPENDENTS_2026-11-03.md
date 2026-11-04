# COLONY-AUTO-04D.4D2A — Physical ART02 Dependent Identity and Life Support

**November 3, 2026. v74. Branch:** `integration/colony-auto04d4d2-v74-20261103`. Based on verified v73 family proof.

## Native world occupancy is not genealogy

ART02 maintains immutable child identity, birth lineage, family residence, legal guardianship, and regional census. It does **not** create `Simulation::NpcRecord` workers. In v73 a full-family rescue could be prepared and audited, but children had no physical occupancy in the canonical Simulation. Claiming completed evacuation would be false: a child could have no physical oxygen consumer or local guardian after the transfer.

v74 introduces the **versioned v59 physical dependent** record: immutable high-ID child identity, current host ship, registration command/tick and existing NPC guardian. It is separate from adult workforce headcounts, work XP and native operating crews. The child exists already in ART02 and the independent `sg_med_society_birth` bridge; physical registration cannot spawn or mint an identity.

### Host-authenticated admission and execution

`Action::HostRegisterDependent` is server-only. The command encodes the ART02 child ID and the single presently responsible guardian ID. `PersistentWorld::submit_host_dependent_registration` submits one canonical command and, under the existing SQLite writer lock, validates the original ART02 birth and independent world-birth receipt, parent values, current family residence, legal nonparent guardian if one exists, absence of death/collision with real NPC identities, and absence of active ferry custody. A unique immutable `sg_colony_physical_child_authority` row ties the command, child, guardian, scope, seed and ship together.

`PersistentWorld::step_with_dependent_registration` is required for actual physical registration. Normal `step()` refuses the due command. The dedicated step executes the deterministic native command in a cloned world and revalidates the birth and admission authority **inside the same SQLite commit transaction** as the new generation snapshot and applied world event. A failpoint rolls back the entire step; no partially registered child or invented person survives.

### Canonical population and resources

Version 59 snapshots store every physical dependent. Decode rejects duplicate high-ID identities, guardian outside the actual ship, unsupported home/location or missing applied host registration event. Independent journal replay recognizes the new command action and reconstructs the exact child identity and location.

Each physical dependent aboard a ship consumes **one physical unit each of local food, water and oxygen** at the ship's existing 20-tick needs interval. The values flow into existing conserved organic waste/wastewater/CO2 accounting. The safety assessment counts registered physical children in the ship's actual local population and shortens its real runway appropriately. **Do not also count the child as a worker.**

### Acceptance

`tests/COLONY_AUTO04D4D2_PhysicalDependentTests.cpp` sets up ART02 parents and a real high-ID birth, independent world-birth bridge, two native ship crews and real stocks. It tests host admission, duplicate refusal, no generic-step bypass, crash-after-snapshot rollback, physical registration, v59 canonical round-trip, no extra NPC worker, local three-person food/water/oxygen consumption, unchanged world material, restart, and independent `recover_copy` reconstruction.

This gate must pass GitHub Actions `.github/workflows/colony-auto04d4d2.yml` before acceptance.

### Remaining work: 04D.4D2B

**The actual complete family relocation still needs a versioned native transfer command and ART02/ECON03 family-census CAS in the same world transaction.** Specifically, the currently registered child must move to the receiving ship alongside each real guardian, ART02 `record_guardian_household_moves` must record the legal residence change, ECON03 regional cold-owner custody must accept a corresponding finite population delta, and a durable final record must prove both the child's new physical consumer and its legal guardian remain one custody chain. A prepared manifest or physical registration alone **does not** mark any colony evacuated. Cross-owner, long-distance, incomplete-household and unverified cold/warm transitions remain refused.
