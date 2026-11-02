# DEMOG-05f2 — Generation-bound transfer preflight

A local C++17 regression against the existing `PersistentWorld` static library passed with `-Wall -Wextra -Werror`.

The read-only transfer preflight now captures a `world_generation` and immutable NPC snapshots (ID, location, home ship, role, farm XP, mentor). A validation helper rejects a receipt when the world generation advances or a recorded NPC property differs. The focused fixture checks fresh receipts, rejection after a world tick, duplicate/missing IDs, and save/reopen.

**This is NOT authoritative embarkation.** A generation-bound preflight is a diagnostic and cannot authorize a physical move: the actual world-owned transaction must revalidate and atomically commit NPC custody with a replayable command/effect record. The current game lacks that public mutation path. Do not treat voyage status as physical arrival.

Next implementation gate: extend the canonical `PersistentWorld` command/snapshot journal with a first-class passenger transfer, preserving world replay invariants, then add physical ship capacity and cross-region tests.