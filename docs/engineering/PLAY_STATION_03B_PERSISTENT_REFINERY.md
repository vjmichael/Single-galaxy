# PLAY-STATION-03B — actual original-world station refining after physical unload

**Scope:** original C++ authoritative simulation and PersistentWorld/SQLite, stacked on PR #120 / stage 03A. **NOT yet a working Godot refinery terminal.**

## New native mechanics

- New opt-in v123 world path \`enable_station_ore_provenance()\` at genesis only; unmodified v122 snapshots retain old onboard geological conversion. The \`Mine\` and automatic mining original-world actions in v123 put one geological tonne in the **ship cargo**, with a separate **provenance annotation**, not another material inventory. Every annotation records source rock, mined command, tick, original ship, actual owner, rock taxonomy, remaining tonnes and next station-transfer receipt. Non-geological bulk ore has taxonomy=3 and cannot masquerade as refined metal.
- Native \`UnloadOre\`, \`LoadOre\` and \`SellOre\` update real ship cargo / owner-private \`StationHangar\` AND move or consume annotation lots; the lots themselves do **not** add mass. The station has exactly one ore stock; other ships cannot get a cloned tonne. A new \`RefineStationOre\` action identifies a prior authentic unloaded lot and consumes exactly one tonne from the corresponding station hangar. It produces 1,000kg station-owned named mineral inventory using the original geological profile, never shipboard \`ColonyShip::minerals_kg\`.
- \`BuildStationRefinery\` is an original authorized world action at a physically player-built outpost with an owned docked colony ship, finite crafted modules, living staff and an installed utility. It consumes **two real ship construction modules** and reassigns one real utility technician to plant work. No default refinery or free labor.
- Station refining requires built plant, correct owner, real docked source ship, recorded station-resident player when registered, an ore lot at that station, and an installed powered utility with positive stored energy. Each batch debits the utility energy by one **and increments consumed-energy lifetime bookkeeping**; cooldown/receipt/stock guard is the tested 03A station batch contract.
- All station lots and plants are canonical snapshot v123 entities. Restore rejects impossible source ship/owner/site inventory custody, station mineral output, nonexistent facility, and overcommitted habitat/refinery/utility staff. \`total_ore\` includes plant embodied material and station mineral kg without duplicating the corresponding tonne.

## PersistentWorld SQLite original authority

\`Simulation::submit\` admits the two new named actions, and the original \`PersistentWorld\` commits them through the same existing \`STEP\` transaction / event journal and \`resource_ledger\`. Install receipts use reason \`station_refinery_install_t\` with four tonnes of existing module equivalent; processing receipts use \`station_refinery_process_kg\` with 1,000kg of same-station input/output. SQLite restore cross-checks ledger reason, operation identity, outcome, quantity, tick and both physical endpoints against actual commands. Tampering causes restoration to fail closed. Original failpoint after snapshot write rolls back the entire pending refinery batch.

## Verified local native evidence — NOT Windows or Godot

- Original C++ \`galaxy_kernel\` compiled in Linux Release configuration, with optimization disabled locally to keep the original 1MB simulation source responsive to test.
- Target \`galaxy_play_station03b_persistent_refinery\` **PASS**, plus three adjacent regressions \`galaxy_play_station03a_refinery_ledger\`, \`galaxy_play_station_boarding\`, \`galaxy_play_station_mining_refining_e2e\`: **4/4 PASS** in CTest.
- Integrated test used actual mined bulk ore to craft 30 modules, built an outpost+utility+habitat, transferred finite food/water/oxygen, staffed the habitat/utility, docked and installed the plant by consuming two modules and a technician, undocked and ship-mined a real geological metallic asteroid, docked and unloaded *that same tagged tonne* into the owned hangar, confirmed zero onboard new minerals, registered a station person, waited for real utility production and refined the physical lot. Original station stock became nickel-iron 860kg, troilite 80kg, olivine 30kg, pyroxene 15kg, other 15kg. \`total_material_kg()\` unchanged.
- Denied unauthorized plant construction, double installation, refine before unload, wrong user and double refine. Proved \`FailPoint::AfterSnapshotWrite\` rollback, SQLite reopened after shutdown, independent \`PersistentWorld::recover_copy\`, and invalid forged processing \`resource_ledger\` amount rejected.
- Exact GitHub file blob SHAs compared with compiled local sources:
  \`Simulation.hpp e74dbe9c088eb2050f1107f89948e3341363fe5e\`
  \`Simulation.cpp 3cf5dd0fb24c49b6db3827e7c551e34eac76275a\`
  \`Persistence.cpp 4cc0c45b22da8c3f9ecae6d434037c9152f69e87\`
  \`PLAY_STATION_03B_PersistentRefineryTests.cpp 6955a7fba95445aac5f818c9270466b3b54c8eed\`.

## Open safety and gameplay gates

- **Cross-hull protected ore custody now tested:** A second owned vessel cannot load or sell the portion of a station hangar represented by another ship's source-linked geological lot. The original-world two-vessel SQLite E2E test verifies a refused LoadOre leaves the hangar, source lot, second ship cargo and world mass unchanged. General transfer between carriers remains a separate design gate.
- Need stronger stock provenance on mixed bulk ore stacks, constructed/refinery power-loss variants, tamper variants and existing-save migration. The v123 path is genesis opt-in until explicit migration and the Windows/Linux CI accepted.
- \`desktop_station_host\` has not yet added a \`REFINERY\` frame, \`BUILD_REFINERY\` / \`REFINE\` operator command, or Godot terminal. **Do not present this as a fully playable station refinery.**
- No exact-head Windows OS compile, CTest, installer or interactive Godot refinery game acceptance yet. See CI on this PR and subsequent 03C/03D.
