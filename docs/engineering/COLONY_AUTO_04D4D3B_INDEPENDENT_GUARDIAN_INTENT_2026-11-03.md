# COLONY-AUTO-04D.4D3B — Independent immutable guardian-intent verification

Date: 2026-11-03. Parent: v77 / PR #14, two physical ART02 households sharing one regional and family scope.

## Defect addressed
The original recovery verifier already checked the prepared intent's generation, physical pre-departure state hash, source/receiver and scope, plus all executed physical and ART02 custody records. It did **not** check the prepared intent's frozen `adult_roster` and `child_guardians` fields after their original transaction. An offline editor could drop the immutability trigger and rewrite one field while leaving the physical simulation and executed member records intact; validation of those other records alone would not detect that specific contradiction.

## Implementation
During world recovery, select and validate the prepared intent's `family_seed`, `adult_roster`, `child_guardians` and prepared status. Only after capturing those fields and verifying the pinned *pre-departure* world state, reconstruct the exact canonical sorted adult roster from the physical departure record. Reconstruct each child:guardian binding from the original physical dependent's birth parent `parent_a`, which is the **only permitted guardian type** in the bounded v74/v77 executable rescue. If either serialized roster differs, reject recovery before claiming that the family safely moved.

This addition does not create a second guardian registry, does not authorize future nonparent rescues, and does not modify resource flow or cold/warm owner behavior.

## Test
The v77 native test still performs the entire two-household atomic evacuation, physical needs, restart and independent copy/replay. It also clones the valid DB twice and deliberately removes the immutable UPDATE trigger in each: one clone changes `child_guardians` to falsely assign the second household's child to the first household; the other substitutes a different fourth adult in the prepared roster. Both must fail closed on `PersistentWorld` reopen. Other regression CTests remain mandatory.

**Status:** draft branch, require passing eight-test Linux CI suite before acceptance.
