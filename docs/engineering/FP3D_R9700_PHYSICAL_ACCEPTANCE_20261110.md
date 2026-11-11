# FP3D-R9700 — physical Windows pilot acceptance and release boundary

## Evidence boundary

This group starts from the already accepted **combined source** `6a0a51d5f7e23d9768c4fe1b2d13fd55e1a18250`. Its Group 2 native/Godot Windows/Linux and WO3–WO5 native Windows/Linux CI gates passed. Those runs **do not** demonstrate an AMD Radeon AI Pro R9700 physical player acceptance. The prior owner report, "I'm stuck in space and cannot move it," remains unclosed on physical hardware. The focused-window keyboard repair is source/hosted-verified, **not** yet owner-verified.

This separate branch adds **observation-only** physical evidence. It does not alter native C++ command authorization, player/ship identity, SQLite writer lease, SGDX custody, resources, native collision, camera authority or production equipment. No automatic release; no private save/minidump upload.

## What the rebuilt self-contained Windows ZIP captures

- In the *actual packaged game launch*, `Launch-PrebuiltStation.ps1` creates a new per-run `playtest-logs/operator-...` directory. Previous sessions' F11 photos do **not** count toward this run.
- The operator may press **F11** to capture only the Godot viewport with hashed original station PERSON or same-tick native SHIP/ROCK observations and rolling 600-frame Godot main-loop spacing.
- Press **F12** at a stuck steering input, unusual camera response, dock refusal or visible stutter. A bounded timestamped marker records native tick, cursor/focus, verified seat, and pacing summary. **F12 is not a ship-control command.**
- A read-only `physical-session-*.jsonl` trace records each *actually sent* original flight verb/request, its verified ACK/OUTCOME/DENY/INTERRUPTED and elapsed monotonic request-to-response time, original ship tick/heading/dock/velocity observations, occasional two-second rolling main-loop spacing, relevant error status and renderer adapter name. It does not log raw keyboard events or desktop content. **An unrecorded input is not evidence the native host was contacted.**
- The Windows launcher also retains local display-controller name and driver version (when CIM is available), separately noting whether an adapter **name** contains `R9700`. Device enumeration alone is not evidence the renderer used that GPU or that input worked.
- Bounded diagnostic trace: 64-row buffered writes and 4 MiB maximum per session. Frame pacing summaries run only every two seconds (not in the hot frame loop). Existing original SQLite remains the sole source of game truth.
- On process exit, the current operator session (not past operator captures) and result summary go into `desktop_godot/SingleGalaxy_Station_Playtest_Evidence.zip`. A zero-exit game window or nonempty ZIP remains `WINDOW-CLOSED-NEEDS-MANUAL-OBSERVATION`, **never** a PASS.

## Actual physical Radeon AI Pro R9700 acceptance matrix

Run the **new exact-head Windows packaged ZIP** only after its own source/Godot/Windows packaging gate passes. Extract to a writable folder and double-click `START_SINGLE_GALAXY_WINDOWS.cmd`. No Visual Studio, Python, Docker, separate Godot or command line is needed. Keep the operator archive private until selecting what to share. Avoid enabling `SGDX_WINDOWS_MINIDUMP`: a crash dump can contain private memory.

1. **Station + boarding:** Original station loads automatically. Press E at the real hatch to board the existing owned ship. Native station PERSON must change to aboard, and the cockpit must obtain a genuine native seat ACK. No free ship, fake teleporter, or second SQLite writer. F11.
2. **Captured-cursor flight:** In the seated pilot view press U to undock. With mouse captured, use A and D for both yaw directions, Up/Down for thrust/reverse, and B for counterthrust. The displayed original ship heading/velocity/position changes only after SHIP receipts. F11 during motion.
3. **Released-cursor flight:** Press Esc to reveal the cursor **without unfocusing the game window**. Repeat A, D, Up/Down and B. Verify real native `original_flight_submitted` and `original_flight_result` pairs with `cursor_captured=false` and corresponding original SHIP heading/velocity/tick. Do not call cursor release a pass merely because the HUD reads “ready.” Test with the window unfocused and verify no *new* input reaches native C++; never interfere with an existing saved maneuver.
4. **Third-person camera:** While aboard, exterior camera remains third-person (V is not a camera toggle). Verify camera follows the original hull through yaw and approach without unpredictable world jumps; compare real asteroid range/bearing with ship/rock telemetry. F11 on observed error.
5. **Rock collision:** Reach a *finite original asteroid*; verify host-owned contact and stopped native velocity, correct tick/rock identity, no phantom ore or fitted mining tool. F11.
6. **Return docking + disembark:** Brake with B, return to the real station, and hold G only inside the host-authorized near-station/low-velocity envelope. Verify original DCK receipt and dock state. Press E **once** after confirmed docking to release the native seat and disembark through the station host. Distant/fast docking refusal must remain real, not be masked.
7. **Restart:** Close and relaunch the same folder. Confirm the same original ship, original resource quantities, command journal, position and authoritative generation, with no duplicate cargo/ore, replacement ship, or replayed collision. Capture F11 before and after where useful.
8. **Pacing:** Observe a sustained 60+ second flight/turn sequence on R9700. Log F12 on visible stutter or stuck response. Review repeated main-loop `p50_ms`, `p95_ms`, `p99_ms`, `frames_over_33ms` and native ACK latency for the corresponding timeline. A displayed 60 FPS does not prove stable delivery or GPU frametime. The metric is Godot main-loop spacing, not GPU hardware timing, so do not claim GPU latency from it.

The player-facing ship flow is automatically station → E board → native seat → U flight → B/G dock → E disembark. **P is not the normal pilot switch** and H is a failed-station retry. V does not toggle from the mandatory third-person mode.

## Exact-release gate

The following must all be true before removing draft/release blocks:

- Full native Group 2 and WO3–WO5 Windows/Linux suites pass against one **same** final source SHA, plus real Godot parser, native scene, bundled Windows ZIP and rendered-frame/tamper evidence.
- The user has actually run the SHA-matched ZIP on physical R9700 with an observed working game, R9700 adapter/driver context, F11 station/flight/dock/restart frames, trace evidence for cursor captured **and** released native flight, and a meaningful paced flight run. An unavailable metric must remain a gap.
- Any failed physical stage is diagnosed from this run's trace and original receipts, repaired in source *without relaxing tests*, rebuilt, and rerun both hosted and physical acceptance at a new exact SHA.
- Physical human acceptance is explicit, not automatically inferred from filenames, device enumeration or zero exit. Keep PR and consolidated release unmerged until then.

## Operator evidence review

The game emits `SingleGalaxy_Station_Playtest_Evidence.zip` on clean exit. The `result.json` preserves package source SHA, bundled engine and supervisor digests, per-run F11 counts, and Windows adapter metadata; `operator-...` contains that run's F11 PNGs/hashed receipts and `physical-session-*.jsonl` trace. Inspect the trace before diagnosing: distinguish no submission (focus/seat/input), DENY/OUTCOME (host authorization), ACK with static ship state (game semantics), and ACK with changed original state but poor visual pacing/camera (presentation). Reject stale or mixed-origin operator captures.

**Status:** Engineering instrumentation added. No new physical R9700 recording received. No physical PASS, no consolidated release authorization.
