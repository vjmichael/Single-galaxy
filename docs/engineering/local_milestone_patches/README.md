# Single Galaxy local milestones — pending source merge

These are verified local milestone patches, preserved on a separate branch so GitHub's existing `main` source remains unmodified. The locally generated d4c3 source and GitHub `main` differ in README and factory-host bridge; naive replay is unsafe. **Do not claim this branch is a fully merged current source checkout.**

Included initially: ECON-03a (resource-only host), ECON-03b (resource market/reclaimer), ECON-03c1 (macro consumption) and ECON-03c2 (cold colony batching). More patch history and a source reconciliation remain open.

The game uses resource inventories, not chits or wages, and plans invite-only multiplayer. The next locally tested milestone ECON-03c3 is an opt-in region event scheduler with shipment escrow and notifications, awaiting authoritative SQLite/live-world integration.
