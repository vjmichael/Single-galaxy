# PLAY-02 — Interactive First Outpost Mission (November 4, 2026)

## Implemented slice
PLAY-02 adds a first-outpost mission panel to the existing `viewer/interactive.html` colony mode. It is not a second game client or a new native simulation subsystem.

The read-only `viewer/opening-guide.js` selects the next user-facing action from host-provided `COLONY STATUS` and confirmed `SCAN_DONE` frames. Its button invokes the *existing* `window.GalaxyColonyCommand` path and its exact native relay allowlist:
`colonyScan`, `colonyMine` (8 tonnes), `colonyRefine` (8 tonnes), `colonyFabricate` (4 modules), `colonyBuild`. It does not authorize an owner, select hidden target IDs, edit SQLite, assume queued commands succeeded, or cache progress in browser storage.

Native STATUS drives step changes. Outpost completion requires authoritative `outposts >= 1`; on relay reset/reconnect the guide refuses to carry forward a previous session's progress. The browser command response remains **queued only**, while the existing native `RESULT` and STATUS stream remains the authority on successful or refused effects. The existing advanced controls remain available below the mission.

## Verification
`node --test tests/OpeningGuideTests.js` tests no-data refusal, the progression stages, restart projection, and outpost completion. The focused PLAY-02 workflow validates Node tests, JS parse and Python relay parse. The previous PLAY-01 native first-outpost restart/inventory test is retained unchanged on the parent branch.

## Remaining acceptance
- Exercise the actual `--colony` local server and graphical Chromium workflow (click through scan→mine→refine→fabricate→build).
- Confirm the native server emits timely STATUS/RESULT and checkpoint reload for each click; verify command refusals visibly explain resource, proximity and crew limits.
- Add a human-playable launch path and reduce prototype-only control clutter; travel/docking and physical movement still need end-user tests.
- Do not claim a full 3D game, completed interactive gameplay, Windows-installed acceptance or autonomous colony growth from the present focused tests.

**Design constraint:** freeze new NPC migration/ferry complexity while progressing from working C++ systems to playable game controls.
