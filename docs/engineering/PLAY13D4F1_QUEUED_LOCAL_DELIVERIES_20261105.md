# PLAY-13D.4F.1 — Queued NPC work-board deliveries (2026-11-05)

## Gameplay and custody rules

The local player-owned work board now accepts **multiple finite posted requests** for an existing planetary site and its actual orbiting colony ship. Players can ask an NPC crew to move 200 kg of nutrients and then 100 kg, without having to manually wait before posting the second contract. Posted jobs are *instructions*, never reservations, independent material inventories, payments, or permission to conjure a carrier.

The authoritative host processes a deterministic job-ID ordered board every world tick. At most **one claimed job per physical planetary shuttle** advances through actual descent, loading from site stock, ascent, and unloading into the parent's real life-support inventory. A second posted job remains pending while a previous job has committed its first flight. Posting never adds more spacecraft, crew, cargo, or infrastructure.

A posted order that cannot execute (missing source stock, fuel, pilot, or berth) remains on the work board, but does not prevent a different posted and physically fulfillable order from advancing. Only a job's **real committed unload** establishes completion. Cancel is permitted only while Posted; an already claimed flight cannot be dismissed to erase cargo, receipts, or NPC custody. Repeating an order ID is rejected even after completion.

## Restart, authority, and mass

All jobs continue to use the canonical v88 snapshot and the existing `PersistentWorld` SQLite commit/rollback. On load, any number of unclaimed Posted jobs are permitted, but two simultaneous active jobs for the same site are rejected as corrupt. Each claimed job still requires its actual assigned NPC pilot and matching shuttle flight receipts. No parallel transport database has been introduced.

The regression test extends `PLAY13D_RegionalFacilitiesTests.cpp` with two consecutively serviced 200/100 kg nutrient orders, identity de-duplication, one-shuttle contention checks, canonical snapshot recovery during flight, a physically refueled third 100 kg order despite an earlier undersupplied 400 kg request, unauthorized/rollback-aware cancellation, and final real inventory and total-material conservation checks. The Windows/Linux native test target already belongs to `.github/workflows/play13d.yml`.

## Remaining end-to-end delivery work

This is **site A → actual parent colony ship**, not yet site A → arbitrary Colony B. The latter is a separate multi-leg logistics gate: the host must identify an existing authorized carrier, confirm its actual spatial route and destination docking/port, load cargo from the parent ship's finite custody, execute physical transit, and atomically unload into Colony B's real inventory. Preserve a single contract reference across legs, with per-leg movement receipts and restart recovery. Do not use the planetary shuttle itself as an interplanetary ship, and do not claim delivery when cargo merely reaches orbit.

Future typed equipment hauling similarly requires a real item-inventory adapter; a valid manifest alone does not create equipment. NPC passenger movement remains subject to existing named-person/guardian safeguards, with temporary surface visits not automatically becoming immigration.
