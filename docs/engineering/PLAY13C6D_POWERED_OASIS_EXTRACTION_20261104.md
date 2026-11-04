# PLAY-13C.6D — Real Installed Oasis Extraction Rig (v76)

## Scope

Convert temporary v75 survey-tool resource collection into **physically equipped, powered, staffed extraction**. This is a real player-built ship-deployed prototype, not yet a permanent planetary mine/outpost.

- Oasis water and nutrient reserves remain finite, explicitly provisioned at world genesis and tied to a real rare planet/moon body.
- The colony ship must physically arrive at the correct local celestial frame before it can install machinery.
- Installation `PersistentWorld::install_oasis_extractor(owner,ship,body)` requires an authenticated existing colony ship, fitted exploration scanner, in-space ready hull, actual reserve, **two already-manufactured 2-tonne modules**, at least two existing crew and food/water/oxygen to support them.
- Two structural modules are removed from the ship and persist as `OasisExtractor.embodied_ore=4` tonnes; total world mass stays constant. No construction gift, duplicate free NPC staff, permanent station or new storage depot.
- `process_oasis_extractors` charges at most one game-energy unit per committed tick, up to 120, using the prospect's solar accessibility. Charging stops if the ship leaves the oasis frame, becomes unavailable, runs out of required life-support resources or lacks crew. This is energy, not invented matter; ship crew still consume normal native life-support stocks every 20 ticks.
- Collection requires an installed operational rig still tied to the exact body, at least two aboard crew, enough *current* food/water/oxygen, and five stored energy units per collected tonne. Collecting 1–10 whole tonnes atomically debits real source kg and credits existing ship life-support kg. Missing conditions fail without side effects.
- Collection is currently direct-to-ship intake under the extractor's custody, not separate site-level storage. Real planetary anchoring, pumps, pipes, power collectors, materials, NPC work schedules and installed station components remain future phases.

## v76 persistence and recovery

`galaxy-kernel-v76` adds `oasisextractor|ship|body|embodied_ore|assigned_workers|condition|energy_units|cycles` records to the canonical saved world. Restore validates equipment bounds, physical original colony ship, existence of the original finite oasis reserve, celestial-system identity and oasis-body identity. Rig modules count toward the conserved SQLite resource baseline, along with the source's remaining stock and transferred life-support contents.

The existing `PersistentWorld` failure/rollback mechanism is used both for installation and collection. No partially-built rigs or extra resources may become visible if the SQLite commit fails. Installation is one-time for a ship under this prototype; a mature transportable equipment lifecycle must explicitly handle unmounting, move/reinstall, equipment damage, ship destruction, cargo disposal and salvage.

The native authenticated host now accepts `OASIS_INSTALL|body` and `OASIS_COLLECT|body|WATER|kg` or `...|NUTRIENTS|kg`. It emits `OASIS_INSTALLED`, `OASIS_EXTRACTOR` status and real `OASIS_SOURCE` quantities. The browser validates and displays the new telemetry. These are native host command paths; GUI command binding and graphical installation controls have **not** been represented as complete.

## Acceptance

The updated `tests/PLAY13C_PhysicalOasisResourcesTests.cpp` now mines **real ore**, refines it, fabricates the two installation modules through the existing industry command path, physically visits a rare source body, proves the old free manual-collection shortcut cannot work, proves wrong-owner/duplicate/failed installation is rejected/rolled back, requires actual subsequent solar charging, then checks source depletion, kilograms in the real ship life-support inventory, unchanged world mass, corrupted rig/sources rejection and SQLite restart, including continuing production after recovery.

The real Windows/Linux `PLAY-13C cosmic/local coordinate acceptance` workflow includes that updated native test. **Native CI is required to pass at exact head before merging the stacked draft PR.**

## Open engineering gates

- An actual built oasis-base station with location-specific equipment and real stored site inventory rather than the ship-bound prototype
- NPC workforce assignments with named individual custody rather than reserving a fixed subset of existing crew count
- Power capacity, equipment wear, repairs, weather and true body-dependent environmental hazards
- Crew-safe unmount, loss, salvage, cleanup and relocation of the real installed machinery
- Retry-safe operation IDs and durable once-only effects for every external extraction request
- Local crop cultivation, nutrient cycling, waste treatment and the previously approved interplanetary compost-return economy

Do not claim full planets, atmospheric simulation, constructed farms, UI installation controls or verified native test results until implemented and accepted.
