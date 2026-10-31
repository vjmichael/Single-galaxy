# Candidate next gates after B3

1. **Close B2 Windows native verification** (compile native MSVC x64, bring verified SQLite source, run all suites, run persistent restart and two-process pipe demo). This remains a cross-platform blocker, not a test claimed complete.
2. **B3.1 scheduling and message runtime:** fixed real-time tick loop independent of presentation framerate; graceful cancellation/shutdown; bounded producer queue; snapshot-on-join and resync; event identity/acknowledgment.
3. **B3.2 graphical prototype:** choose renderer deliberately; draw a ship and asteroid as simple debug shapes; show position and ore data, then entity removal and cargo-full message. No EVE assets.
4. **B2.9 durability scale:** snapshot compaction/retention policy, performance under hundreds/thousands of entities, duplicate-work safeguards.
5. **B4 finite-stock NPC economy** when reliable tick scheduling and state persistence baseline are satisfactory. Maintain one system at first inside the single-fictional-galaxy design.
