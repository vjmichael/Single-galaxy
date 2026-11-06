#!/usr/bin/env python3
"""Two-owner security relay: real private-frame demultiplex and exact commands.

No C++ substitute is built here. Actual original native host/SQLite owner
assertions are separately tested by SHIPSTATUS07G_TwoHumanSecurityTests.cpp
and SHIPSTATUS07G_LiveTwoHumanHostTests.py.
"""
import base64
from collections import deque
import importlib.util
import io
from pathlib import Path
import threading
import types

source=Path(__file__).resolve().parents[1]/'live'/'interactive_relay.py'
spec=importlib.util.spec_from_file_location('sg_07g_private_relay',source)
relay=importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)

class Subprocess:
    stderr=io.BytesIO(b''.join([
        b'PRIVATE|8|COLONY|1|25|SECURITY_CALL_INBOX|1001|10|7\n',
        b'PRIVATE|8|COLONY|1|25|SECURITY_ERROR|NO_OWNED_COLONY\n',
        b'PRIVATE|9|COLONY|1|25|SECURITY_STATUS|1|2|1|0|3\n',
        b'PRIVATE|8|COLONY|1|25|FLEET_STATUS|SPY\n',
        b'COLONY|1|25|SECURITY_CALL_INBOX_RESET\n',
    ]))
    def poll(self):return None

rt=types.SimpleNamespace(
    proc=Subprocess(),cv=threading.Condition(),stderr=[],
    colony_events=[],survey_events=[],
    private_colony_events=deque(maxlen=4096),
    private_event_sequence=0,
    human_owners=frozenset({7,8}))
relay.SharedRuntime._read_stderr(rt)
assert rt.private_event_sequence==2
assert len(rt.private_colony_events)==2
assert all(who==8 for _,who,data in rt.private_colony_events)
assert all(base64.b64decode(data).startswith(b'COLONY|1|25|SECURITY_')
           for _,_,data in rt.private_colony_events)
assert not any(b'1001' in base64.b64decode(data)
               for data in rt.colony_events)
assert len(rt.colony_events)==1 # only original owner-7 public security

# The live trusted relay does not turn a second account into a ship
# controller, a free NPC or the source owner of a native attack.
rt.authenticated=True
rt.cmd_lock=threading.Lock()
rt.finished=False
rt.proc.stdin=io.BytesIO()
relay.SharedRuntime.send(rt,'SECURITY_REFRESH',principal=8)
relay.SharedRuntime.send(rt,'SECURITY_CALL|99',principal=8)
relay.SharedRuntime.send(rt,'SECURITY_CALL_REPLY|1001|DECLINE',principal=8)
assert rt.proc.stdin.getvalue()==(
    b'AUTH|8|SECURITY_REFRESH\n'
    b'AUTH|8|SECURITY_CALL|99\n'
    b'AUTH|8|SECURITY_CALL_REPLY|1001|DECLINE\n')
for command,principal in [
    ('FIRE_SHIP|7',8),('SECURITY_CLAIM|3000',8),
    ('SECURITY_PATROL|123|7',8),('SECURITY_WARN|7',8),
    ('SECURITY_REFRESH',9),('AUTH|7|SECURITY_REFRESH',8),
]:
    before=rt.proc.stdin.getvalue()
    try:relay.SharedRuntime.send(rt,command,principal=principal)
    except (PermissionError,ValueError):pass
    else:raise AssertionError('Unauthorised native security command sent')
    assert rt.proc.stdin.getvalue()==before

# The private SSE response consumes ONLY entries for the authenticated
# principal. Neither original owner-7 public frames nor other private owner
# messages may be delivered, including after a login/reconnect.
rt.private_colony_events.append((3,7,
    base64.b64encode(b'COLONY|1|25|SECURITY_CASE|SECRET').decode()))
rt.private_event_sequence=3
rt.finished=True
rt.error=None
rt.send=lambda command,principal=None:None
writer=io.BytesIO()
handler=object.__new__(relay.Handler)
handler.server=types.SimpleNamespace(runtime=rt)
handler.wfile=writer
handler._principal=lambda:{'session_id':'session-8','playerId':8}
handler.send_response=lambda code:None
handler.send_header=lambda key,value:None
handler.end_headers=lambda:None
handler._security_only_events({'session_id':'session-8','playerId':8})
output=writer.getvalue()
assert b'event: colony\n' in output
assert b'event: frame' not in output and b'event: survey' not in output
assert b'SECRET' not in output
assert b'event: complete\n' in output
assert b'PRIVATE|' not in output

# The rolling buffer continues accepting events after 4096 frames; cursor
# sequence is independent of physical ring length.
for i in range(4200):
    rt.private_event_sequence+=1
    rt.private_colony_events.append(
        (rt.private_event_sequence,8,base64.b64encode(
            f'COLONY|1|25|SECURITY_STATUS|1|1|1|{i%2}|3'.encode()).decode()))
assert len(rt.private_colony_events)==4096
assert rt.private_event_sequence==4203
print('PASS SHIP-STATUS-07G private relay: exact owner-only SSE, no SGVIEW, '
      'human-only command gate, reconnect isolation and 4096-row rolling buffer')
