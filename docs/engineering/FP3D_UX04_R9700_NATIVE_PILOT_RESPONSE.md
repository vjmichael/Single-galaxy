# FP3D-UX-04 — Radeon AI Pro R9700 pilot response and mining gate

**Status:** Implementation candidate, not yet native-run accepted. Owner's real Windows 3D game launch, boarding, seat and undock are demonstrated; complete flight/dock/disembark, sustained smoothness and physical rock encounter are **not** approved. The original C++ simulation, SQlite world and SGDX supervisor remain sole authorities.

## Evidence / diagnosis

Three owner's real Windows screenshots (2026-11-09):
- Actual game launches on physical Radeon AI Pro R9700, with the native SGDX-controlled ship #2 **IN SPACE**, player **SEATED**, 60 reported display FPS/60Hz physics, yaw 45° → 60° → 195°, ship positions from (4.5,-6.8,0)m through (-139.5,-895.4,0)m. Turning commands **reached native C++** despite inadequate visible yaw cues. "Very choppy" remains subjective until measured by native acknowledgement intervals and frame-time evidence; display FPS alone cannot establish motion cadence.
- Native flight action #246 was denied; the host's refusal is preserved in UI rather than silently accepted. A return-docking attempt can be denied when far from the real station or traveling too fast; no fake dock is authorized.
- The genuine ROCK/ROCKNONE sensor reports no rock within the 30m native horizon once far away. Cargo is 0/12t; **mining tool NOT FITTED**. This is a real equipment gate, not a missing shortcut. Do not create decorative ore, optimistic cargo, a fabricated miner, or bypass fitting to disguise this gap.

## UX/host-interface repairs

- Map focused/captured pilot-seat horizontal mouse movement to bounded RIGHT/LEFT native **intent**, retaining A/D keyboard yaw and native original seat, session, tick, rate and request authorization. Mouse movement never writes velocity or yaw directly.
- Keep **G** available while physically held and a pending flight command completes; one-frame-only G previously could be silently lost to the 110ms native client gating. Restrict U/G submissions to the observed trusted dock state; real C++ still authorizes proximity, speed and custody.
- Noncolliding fixed **inertial bearing dots** in the exterior preview allow yaw to be perceived with no authenticated asteroid within 30m. These are explicitly **not** contacts, resources or collision proxies. Keep white ship and gray station colors.
- Easing affects only the **spectator camera transform**; the ship's exact Godot hull yaw still follows the committed original C++ SHIP receipt without extrapolation and without replacing the original simulation.
- Buffer bounded diagnostic JSONL to flush at most every 500ms or on 64 receipts/exit; reduce synchronous Godot per-SHIP/ROCK disk work. This is not a second SQLite world, not an approval or an AI runtime.
- Show discoverable mouse/A,D yaw and true U/G/B/up/down controls, with explicit **MINING LOCKED — original C++ equipment not fitted yet**.

## Acceptance / remaining work

1. Run **one** grouped exact-head native Windows/Linux build and CTest (19 original cases), strict UTF-8 Unicode nested-path reopen, supervised worker/crash recovery, real Godot E BOARD→cockpit→UND→DCK→E EXIT, engine smoke for inertial visual-only markers/mouse classifier, and packaged Windows ZIP/tamper/real viewport acceptance.
2. Require independent physical R9700 playtest on that new exact SHA: repeated smooth A/D **and mouse** yaw, visible camera yaw against fixed stars, confirmed seat and original C++ ship heading, U undock, verified actual asteroid within 30m and physical contact, B brake then G dock near original station, stand E EXIT, reopen saved SQLite unchanged. Capture F11 receipts and frame pacing/observed response. Do not claim human acceptance based on CI alone.
3. Only after the flight/dock physical gate passes, resume **native fitted mining equipment → mine → authoritative cargo → dock → unload → refine**, with persistent finite mass and no fabricated client resource.

The on-screen 60 FPS and one denied C++ command alone do not determine exact GPU/driver performance: no physical native latency measurements are available yet.


## FP3D-UX-05 — player cannot move ship on physical R9700 (2026-11-10)

- The exact-head hosted **#42774447114** gate succeeded (native MSVC/Linux 19/19, original SGDX/SQLite process restart, actual Godot, verified prebuilt Windows ZIP and real windowed frames). That is a **hosted gate only**. The creator subsequently reported **"I'm stuck in space and cannot move it."** Therefore player-facing flight and physical Radeon AI Pro R9700 approval are **FAILED/PENDING REPAIR**, not accepted. Do not re-label the old ZIP as repaired.
- Source diagnosis: every keyboard flight intent was nested under `Input.mouse_mode == Input.MOUSE_MODE_CAPTURED`; releasing mouse control (Esc, UI or OS interaction) could silently suppress **all** A/D, Up/Down, B and U/G commands despite the original C++ saved pilot still being seated. We have not established that this was the only cause of the owner's report.
- FP3D-UX-05 repair: a focused window with C++-acknowledged pilot seat/aboard custody may issue **keyboard intent even when cursor visible**; captured mouse remains mandatory for *relative mouse yaw only*. No flight command is emitted when the window is unfocused, seat was never acknowledged, or person remains on foot. C++ still verifies each submitted flight request and owns actual ship pose/collision/resources; no second SQLite writer or teleport.
- Added original-Godot intent smoke with synthetic pressed keys while `Input.mouse_mode=MOUSE_MODE_VISIBLE`, including FWD/LEFT permitted, U/G dock-state boundary, and negative unseated/on-foot cases. HUD now separates `PILOT WAIT`, `PILOT PAUSED`, `KEYBOARD FLIGHT READY` and `PILOT READY`. Source-only checks must not be reported as real Godot/native/GPU acceptance.
- **Acceptance sequence:** one grouped exact-head Windows/Linux 19/19 native gate; genuine original C++ E BOARD → SEAT → UND → FLIGHT → DCK → single E EXIT; verified Windows ZIP and 4 on-screen frames; then owner-operated R9700 input with cursor visible/captured, on-screen yaw/velocity/position changes, and return docking. Collect F11 viewport receipts if still stuck; inspect native DENY/status/ship epoch and original conserved save. Never silently respawn or delete player SQLite. See [issue #126](https://github.com/vjmichael/Single-galaxy/issues/126).
