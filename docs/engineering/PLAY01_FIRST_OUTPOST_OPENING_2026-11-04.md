# PLAY-01 — First-outpost opening regression gate (November 4, 2026)

**Scope decision:** freeze migration complexity after EXIT-01D; EXIT-01E remains an unmerged experimental route-selection draft, not a prerequisite for playing the game. Focus on one starting colony ship, finite crew, useful mining, onboard refinement and manufacturing, building one real outpost, and durable continuation.

This milestone adds one focused end-to-end *native simulation* regression `tests/PLAY01_FirstOutpostRegressionTests.cpp`. Unlike one uninterrupted unit-test path, it closes and reopens the SQLite world at distinct player-visible progression checkpoints: after mining, after refining/manufacturing, after construction. It checks conserved physical material and original crew, enforces command-id replay refusal, and validates a separately recovered copy of the finished world.

This is intentionally **not** a new mine/market/transport engine. It reuses existing `PersistentWorld` actions and the existing B70 colony-first mechanics. The test should run in CTest and focused GitHub Actions. Only report success once the exact revision passes.

## Current limitations

The fixture uses direct authoritative native commands; it is **not a human-controlled desktop or Chromium UI playthrough**. It does not validate docking, travel, field scanning, a responsive player-facing tutorial, graphics, Windows input, or the final intended continuous 3D galaxy. The first-outpost build consumes resources through existing prototype rules.

## Follow-on PLAY-02

Integrate the same ship→resource→production→outpost journey into the interactive client and test the player-facing path, including command acknowledgments, progress, errors, save/load, and player navigation. Avoid any new society subsystem until the player can actually complete this core experience.
