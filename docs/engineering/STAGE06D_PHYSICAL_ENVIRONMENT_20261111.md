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
  a maximum of one cycle per 20 committed ticks. Read-only physical survey
  uses the existing owner/ship/fitted scanner and source-region gate.
  Hazards do not automatically invent damage, power costs or new workers.
- New-game colony/station genesis enables the layer after original 38-tonne
  S6-C allocation; recovered historical worlds receive only original-material
  subaccounts. Genesis placement closes once v128 physical processes begin.
- Canonical v128 parsing refuses extra/missing/duplicate phases,
  negative or out-of-budget water/soil, invalid hazards and future ticks.
  total_ore sums the original finite source stock exactly once.

## Source-staged regression

Stage06PhysicalEnvironmentTests.cpp / galaxy_stage06_physical_environment:
ice/water, aquifer, local soil erosion, hazard, conservation, zero-material
worlds, rejection of repeat/overbudget transfers, unauthorized scanner
denial, v128 reparse and missing-row refusal, committed recovery,
original SQLite v127 upgrade and failure-injected rollback.

No native C++ build, CTest, CI, installed Godot or physical Windows Radeon
operator acceptance was executed at this source head. Compiler and runtime
acceptance remain OPEN.

## Remaining engineering

1. S6-D5 actual authorized extraction remains LOCKED. Debit finite region
   source and the matching physical phase once, credit exactly one existing
   colony-local inventory and verify original owner, NPC skill, equipment,
   energy, legal site access, cancellation, journal and replay receipts.
   A scan cannot grant extraction rights. Never duplicate OasisReserve water.
2. Across-region surface runoff, ocean exchange, groundwater connectivity and
   sediment transport require real debit/credit between physical region keys.
   Current source conservation and processes are local.
3. S6-E: attach hazard consequences and actual water/energy requirements to
   original machinery, farms, industry and named-worker schedules.
4. S6-G: complete native Windows/Linux compile/CTest, old SQLite upgrades,
   full recovery/load matrix, installed Godot UI and physical Radeon acceptance.

S6-D1–D4 source present; D5 and cross-region/industry integration OPEN.
