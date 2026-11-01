# ECON-03c3 regional scheduler — local engineering handoff

Local prototype adds a deterministic opt-in C++ regional scheduler. It reserves real material at dispatch, moves shipments by scheduled departure/arrival timestamps, advances `ColdColonyNeeds` algebraically, and records chronological notices for shortage, recovery and cargo events. State update is transactional on invalid arrival. No NPC per-tick loop.

Local focused compatibility results: 4/4 CTest cases passed (ECON03b, ECON03c1, ECON03c2, ECON03c3). The new scheduler is not connected to live SQLite, UI notifications, real ship movement or production events. Source archive and patch delivered separately in the conversation.

Next integration: durable event/notification journal, real NPC cargo handoff, and production/utility events. GitHub is a repository and documentation record only.
