# Ship modules — DEFERRED until the 3D world works

**Creator decision: 2026-11-08.** This is a scheduling and design constraint, not an instruction to ship modules now.

Single-Galaxy's next priority is a genuinely working original 3D world: physically coherent space geometry and ship interactions, ship/asteroid collision, camera and cockpit/exterior parity in Godot, station boarding/undocking/flight/return, and real Windows playtesting. The original C++ simulation + SQLite remains authoritative. Do not spend the next task group expanding module racks, crafting recipes, power/heat systems or outfitting UI.

**Existing 04B draft:** PR #124 contains experimental v124 material-backed mining-drill logic, installation and one-tonne extraction tests. Preserve this work for reference; it is **not approved as the final module system**, does not unblock physical-world acceptance, and must remain draft until revisited after 3D is accepted. 04B source-only checks and queued CI are not proof of installed gameplay. Do not auto-gift mining lasers, modules or ships to make the demo appear functional.

## Ordered milestones

1. **FP3D-ENV-01 (issue #114):** native ship–asteroid swept 3D collision and deterministic contact receipts (include direct hit, high-speed tunneling, tangent pass, restart, local-frame isolation and OS-native CI).
2. **Playable 3D:** authoritative ship and rock poses rendered correctly by Godot; camera, cockpit/exterior transitions, physics and station collision alignment, usable controls; original world retains all authority. Confirm with actual Godot runtime and physical Windows gameplay, not synthetic frames.
3. **Non-module playable loop:** actual station spawn -> board -> undock -> steer -> encounter real rock -> return/dock/disembark and reopen saved world. An unfitted ship may explore and fly, but it may **not** mine resources.
4. **Only after prior gates:** decide ship module sizes/slots, physical manufacturing, energy, heat, equipment installation, maintenance, salvage and UI; revive/refactor 04B mining tool and the one-tonne cargo/refining vertical slice in an independently reviewed branch.

## Branch and acceptance notes

The active gameplay branch PR #124 is already ahead of PR #123 (04A) with no missing 04A commits. As checked on 2026-11-08, repository `main` is an ancestor of the staged gameplay work, thousands of commits behind. Do not force a giant unverified draft-stack merge into `main` merely to claim the repository is current. Keep the work in GitHub, stacked or separate by task, and merge progressively only after native CI and installed gameplay evidence.

For 3D-world-first work, branch from PR #123 (04A) so the still-draft 04B module system is not imported as a prerequisite.
