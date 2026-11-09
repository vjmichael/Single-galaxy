#!/usr/bin/env python3
"""Source-only guard, NOT a substitute for Godot engine or native runtime."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
game=(root/'world/main.gd').read_text()
session=(root/'world/native_station_session.gd').read_text()
terminal=(root/'world/refinery_terminal_ui.gd').read_text()
cpp=(root.parent/'src/desktop_station_host.cpp').read_text()
projection=(root.parent/'include/galaxy/StationRefineryProtocol.hpp').read_text()
smoke=(root/'tests/native_station_refinery_protocol_smoke.gd').read_text()
checks={
  'source-driven-native-station-refinery': 'Action::RefineStationOre' in cpp and 'world.submit(c)' in cpp and 'world.step()' in cpp,
  'station-person-distance-gate': 'station_refinery_protocol::accessible(person)' in cpp and 'kTerminalRadiusMm=700' in projection,
  'no-trusted-client-actor': 'c.actor=plant->source_ship' in cpp and 'c.issuer=kOwner' in cpp,
  'original-station-source-readback': 'world.world().station_ore_lots()' in projection and 'world.world().station_refinery(station)' in projection,
  'original-private-hangar-readback': 'world.world().hangar(station,owner)' in projection,
  'bounded-original-lot-display': 'kMaxVisibleLots=48' in projection and 'REFEND|3|' in projection,
  'frame-parser-atomic-ack': '_accept_refinery_line(f)' in session and 'REFHEAD' in session and 'REFLOT' in session and 'REFMIN' in session and 'REFEND' in session,
  'no-optimistic-inventory': 'refinery_received.emit(state)' in session and 'station_refinery_protocol::make_snapshot' in cpp,
  'refinery-command-receipt': 'submit_refining(lot_id: int)' in session and '_pending != 0' in session,
  'station-terminal-mesh': 'Station refinery terminal (R)' in game,
  'visible-actual-lot-picker': 'get_item_metadata' in terminal and 'refine_requested.emit(source_lot)' in terminal,
  'no-godot-refining': 'Action::RefineStationOre' not in game and 'world.step()' not in game,
  'native-world-UI-source': 'refinery_terminal.present(snapshot)' in game and 'refinery_received.connect(_on_native_refinery_state)' in game,
  'no-world-mint-on-open': 'native_station_session.request_refinery_view()' in game,
  'godot-synthetic-smoke': '03C synthetic original SQLite refining ACK' in smoke,
}
for name,ok in checks.items():
  assert ok,name
print(f"PLAY-STATION-03C source checks PASS {len(checks)}/{len(checks)}; engine and C++ tests separate")
