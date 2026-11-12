# S6-D — Physical Environmental Processes (v128 original world)

Date: 2026-11-11. Branch: sg/stage6d-physical-environment-20261111.
Status: SOURCE STAGED, not native accepted. No merge or release authorized.

## Original-world integration

- Existing original planets, the S6-C v127 immutable generator/body/region
  identity and finite geological stock remain authoritative.
- S6-D opt-in upgrades to snapshot v128. planetprocess is an original
  Simulation::canonical_state row; PersistentWorld commits it using the
  EXISTING SQLite generation, original mass source, world hash and rollback.
- A physical row is an exact subaccount of the original finite planetresource
  row, not another source or shipping inventory.
  Water: liquid + ice + aquifer + retained atmosphere = original water kg.
  Soil: stable topsoil + local sediment = original soil kg.
  Minerals and volatiles remain unchanged and unmined.
- D1: bounded freezing/thawing transfers literal physical water kilograms.
- D2: infiltration, local aquifer storage/release, evaporation and condensation
  operate only on material already present. Climate rainfall/temperature/
  runoff scores are rate/hazard signals, NOT physical kg.
- D3: risk-governed local soil erosion/stabilization move exact soil kg
  between topsoil and retained sediment (no new fertile earth).
- D4: deterministic per-region cold/heat/flood/drought hazards persist on
  a maximum of one cycle per 20 committed ticks. Read-only physical survey is sent by the existing native PLANET_SURVEY command as PLANET_PHYSICAL, strictly parsed by viewer/colony.js and source-mass-checked in the same native tick; it uses the existing owner/ship/fitted scanner and source-region gate.
  Hazards do not automatically invent damage, power costs or new workers.
- New-game colony/station genesis enables the layer after original 38-tonne
  S6-C allocation; recovered historical worlds receive only original-material
  subaccounts. Genesis placement closes once v128 physical processes begin.
- Canonical v128 parsing refuses extra/missing/duplicate phases,
  negative or out-of-budget water/soil, invalid hazards and future ticks.
  total_ore sums the original finite source stock exactly once.

## Source-staged regression

Stage06PhysicalEnvironmentTests.cpp / galaxy_stage06_physical_environment, plus PLAY13D_PlanetSurveyNodeTests.js strict PLANET_PHYSICAL decoding:
ice/water, aquifer, local soil erosion, hazard, conservation, zero-material
worlds, rejection of repeat/overbudget transfers, unauthorized scanner
denial, v128 reparse and missing-row refusal, committed recovery,
original SQLite v127 upgrade and failure-injected rollback.

No native C++ build, CTest, CI, installed Godot or physical Windows Radeon
operator acceptance was executed at this source head. Compiler and runtime
acceptance remain OPEN.

## S6-D5 — Original finite source extraction (v129)

Status: SOURCE IMPLEMENTED provisionally in the existing Simulation and
PersistentWorld; native compile, real positive extraction and installed
acceptance still OPEN. Existing OasisReserve extraction is unchanged.

- The immutable S6-C registered source remains the *only* geological input.
  An authorized source operation moves original physical kilograms into the
  actual preexisting PlanetarySiteFacility at the *same exact body+region*.
  No long-range teleport to a ship, global inventory or second material ledger.
- Water transfers actual liquid surface or groundwater phase kg into the
  site.water_kg inventory. Ice and atmospheric moisture cannot be collected as
  liquid without another separately conserved phase process.
- Stable topsoil transfers into the site.local regional_geology_kg[0], and
  mineral/volatile deposits into [1]/[2] at the original constructed site.
  Source stock_kg is irreversibly debited by precisely the credited amount.
  Physical phase balances are debited in tandem for water/soil.
- Admission requires authenticated owner, correct original ship and anchored
  body, matching previously owner-discovered original geography, a real
  commissioned on-site extractor (not automatically built from a survey),
  production project completed, powered surface utility, installed working
  equipment, no unsafe flood/cold/heat, paused autonomous policy, one original
  Shape-certified healthy physically deployed NPC, bounded whole-tonne amount
  and sufficient actual source stock. Groundwater requires double electricity.
- One unique request ID yields one canonical immutable planetrelease receipt:
  worker, owner, ship, source body/region/revision, exact resource and phase,
  kg, electricity debited and committed tick. Replays, stale/cross-owner
  requests and insufficient inventories are refused before mutation.
- V129 canonical restore validates ORIGINAL funded kg as:
  body.original_kg = body.unallocated_kg + sum(region remaining stock_kg)
                      + sum(immutable extraction receipts kg).
  It verifies each physical water/soil phase subaccount against remaining
  geological stock. The original-world material origin still counts site
  inventory once, and original region source once; receipts never add mass.
  Empty depleted source rows are real zero inventories, not regenerated ore.
- All authorized transfers are candidate-and-commit through the SAME existing
  PersistentWorld SQLite transaction and generation CAS, including injected
  failpoint rollback. No separate local DB, synthetic NPC, or resource writer.
  Pre-commit refusal/cancellation has no effects; an accepted receipt is
  irreversible rather than refunding mass upon replay.
- Native PLANET_GEO_EXTRACT and authenticated relay action
  colonyPlanetGeoExtract are separate from PLANET_EXTRACT (legacy OasisReserve).
  Owner survey records remain mandatory. The web UI exposes selected source,
  water phase, amount and qualified worker. PLANET_SOURCE_TRANSFER is strictly
  parsed and displayed. Original shuttle water transfer remains available;
  the new on-site soil/mineral/volatile inventory is not automatically cargo
  aboard a spacecraft.

## Source-staged D5 regression

- Stage06AuthorizedSourceTests.cpp and the CMake target
  galaxy_stage06_authorized_source: canonical v129 migration, original finite
  source initialization, denied cross-owner / ice / missing machinery / NPC /
  non-whole-tonne extraction, unchanged snapshot and material origin on denial,
  forged release refusal, old v128 SQLite rollback injection, idempotent upgrade
  and restart.
- PLAY13D_PlanetSurveyNodeTests.js: strict original-source transfer receipt
  decoding, invalid phase and malformed transfer refusal, complete depletion
  readback and host/relay/UI route structure.

**Not run:** native compiler, CTest/Node, genuine positive equipped-site
extraction with a qualified NPC, Windows/Linux host end-to-end, installed
Godot owner-operated workflow and historical multi-world stress. Source
staging alone is not passing acceptance.

## Remaining downstream Stage 6 work

1. Cross-region watershed runoff, groundwater connectivity, ocean exchange
   and sediment movement require independent physical region-to-region debit
   and credit under the same original world transaction.
2. S6-E core ecology/industry source integration now staged in v130:
   crop soil/moisture and hazard constraints, actual utility power, worker
   availability, extracted on-site soil and WO4 original shortage observation.
   Full ecological succession and onward original non-water shuttle/freight
   remain OPEN. See [S6-E ledger](STAGE06E_ECOLOGY_INDUSTRY_20261111.md).
3. S6-G: consolidated exact-head Windows/Linux kernel, native-host, CTest,
   SQLite recovery/corruption, long-running performance, Godot installed,
   owner physical Radeon and human acceptance. All OPEN.

S6-D1–D5 SOURCE STAGED; native/installed acceptance OPEN. No merge/release.
