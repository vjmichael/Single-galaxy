# ECON-02B1c — Cubic-metre cargo capacity and exact factory stack transfers

Date: 2026-10-31 (local Linux prototype). Baseline: ECON-02B1b two-machine persistent outpost line.

## Delivered

- **Game-volume only:** ships retain `cargo_capacity` as a scalar interpreted as cubic metres, not a maximum payload mass. A new `ship_cargo_volume_liters(ship_id)` query exposes exact occupancy without rounding. One cubic metre equals 1,000 internal litres, making single-unit factory transfers possible. No density, acceleration or structural-mass restrictions are imposed on ship cargo.
- **Explicit, provisional game balancing volumes:** every legacy bulk ore/refined unit is treated as 1 m³; each legacy construction module as 2 m³; every currently modeled kilogram-quantity material stack as 1 L per unit. These are deterministic item-volume coefficients, **not physical densities**. Future catalogue entries can override volumes without a density system or a payload-weight limit, provided save/version migration is handled.
- **Independent exact stacks:** new authenticated `TransferWorkshopStack` action, appended after all existing enum values to preserve prior action IDs. `thrust_x` selects 0 aluminum, 1 panels, 2 frames, 3 scrap. Positive `amount` moves ship→local workshop; negative moves local workshop→ship; 1–1000 units each command. Physical stock, 1,000-unit site buffers, per-item ship quantities, occupied volume and provenance move atomically. It is **not** a teleport: it uses the prior outpost ownership, same-system, ship proximity and authorized command checks.
- **No pallet bundling needed for new actions.** A lone panel or scrap unit can be shipped independently, or moved back to the workshop. Old pallet actions remain accepted for backwards compatibility but are no longer required for new workflows. The older tonne-based resource accounting remains in geological mining/refining/recipe design and is **not** a cargo weight limit.
- **No free resources:** inadequate source stock rejects the transfer; a full cargo hold rejects inbound goods with `CargoFull`; insufficient destination bin space rejects it. No partial transfer occurs on a rejected command. Real physical world material conservation remains verified despite individual ship stacks no longer being multiples of 1,000 units. This required combining globally conserved kilogram leftovers *before* the ore-equivalent divisibility check.
- **Durable evidence:** canonical snapshot `galaxy-kernel-v24`; v1–v23 snapshots remain readable. The same SQLite transaction records the source/destination, exact item kind, signed action and amount, event, and updated world state. Recovery re-derives and checks endpoints against the command journal; tampered ledger destinations fail restoration.

## Local tests performed

- Complete Linux Ninja/Debug build: PASS.
- Native CTest **72/72** (including older live scenarios and new ECON-02B1c integration): PASS.
- Node.js **84/84**: PASS.
- Python design **3/3**: PASS.
- Dedicated new integration exercises original mined/fabricated aluminum, real Tier 1 workshop, individual present NPC operators, exact 37-unit raw feed and return, independently shipped 1-unit panel and scrap, 7-unit frame and return, exact volume accounting, filled ship rejecting another one-unit transfer, one-unit freed volume accepting a one-unit transfer, v24 snapshots, SQLite restore/recovery copy, forged resource ledger rejection, and total material conservation.
- Existing ECON-02B1a and ECON-02B1b integration tests also remain green.

## Explicitly still open

- True configurable catalogue of **per-item m³ volumes** in the user interface, item counts for discrete manufactured parts, and a graphical cargo-hold inventory view.
- Generic location-to-location stock transports, independently installed bin/conveyor/lift objects, route speeds, and separated player-constructed factory spatial placement.
- Refactoring older legacy ore/hangar systems to the universal stack-store API; existing geological tonne quantities remain internal recipe/source accounting and UI may need conversion adapters.
- Native Windows GUI and complete player-operated interactive acceptance, large-scale remote factory event-driven fast-forward, multiplayer authority/transport and performance profiles.

## Follow-on gate: ECON-02B2

Unify ship, workshop, outpost storage and containers behind an item-catalog-backed stack inventory and finite-volume API, and then apply bounded rate/route transfers to a physical two-machine line. Retain hard input minima, output/scrap backpressure, NPC authority and deterministic ledger replay. Avoid arbitrary factory floor caps or payload-weight systems.

GitHub is source/provenance repository only. Development and testing occur locally.
