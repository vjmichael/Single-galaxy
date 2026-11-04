# PLAY-03 — Piloting and Mining Controls (November 4, 2026)

PLAY-03 is a small browser gameplay slice layered directly on PLAY-02. No new simulation engines, NPC transport rules or world-writing authority.

- The first-outpost tutorial is **optional**. Skip hides only the guidance; all mining, movement and production controls remain available. Show tutorial restores it. The user's preference is stored locally when permitted; inaccessible storage degrades harmlessly. Skip does not mutate a save, force progression, grant items, or disable the native host.
- Flight deck maps arrow keys and WASD to existing authoritative `thrust` commands; Space requests neutral drift. Keyboard shortcuts are ignored when interacting with text fields, selections, buttons, contenteditable controls, or when modifier keys/key repeats apply. UI buttons remain available.
- The flight deck displays current physical coordinates, speed and nearest visible asteroid using world projection frames. This range is *advisory only*; the native simulation continues to enforce mining range, capacity and correct target ownership.
- A selected scanned rock is mined through the existing `colonyMineTarget` host action, with ID populated only from validated native geological scan records. Direct nearest-rock mining retains the existing `colonyMine` path.
- No client-side completion is inferred from HTTP 202. Native result and STATUS telemetry determine success, and the native persistent world remains the only writer.

Validation: `node --test tests/FlightControlsTests.js`, existing `OpeningGuideTests.js`, JS syntax checks, and subsequent live authenticated relay/browser smoke. A full physical keyboard and Windows Chromium acceptance remains open.
