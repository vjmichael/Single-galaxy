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
relay.SharedRuntime.send(rt,'SECURITY_NOTIFICATION_ACK|41',principal=8)
assert rt.proc.stdin.getvalue()==(
    b'AUTH|8|SECURITY_REFRESH\n'
    b'AUTH|8|SECURITY_CALL|99\n'
    b'AUTH|8|SECURITY_CALL_REPLY|1001|DECLINE\n'
    b'AUTH|8|SECURITY_NOTIFICATION_ACK|41\n')
for command,principal in [
    ('FIRE_SHIP|7',8),('SECURITY_CLAIM|3000',8),
    ('SECURITY_AGENCY_ESTABLISH',8),
    ('SECURITY_AGENCY_REFER|5005',8),
    ('SECURITY_PATROL|123|7',8),('SECURITY_WARN|7',8),
    ('SECURITY_REFRESH',9),('AUTH|7|SECURITY_REFRESH',8),
]:
    before=rt.proc.stdin.getvalue()
    try:relay.SharedRuntime.send(rt,command,principal=principal)
    except (PermissionError,ValueError):pass
    else:raise AssertionError('Unauthorised native security command sent')
    assert rt.proc.stdin.getvalue()==before

# A new signed-in stream MUST NOT replay earlier volatile notifications
# even when those events belong to the same playerId. Only the native
# SECURITY_REFRESH result from committed SQLite is current.
rt.private_colony_events.append((3,7,
    base64.b64encode(b'COLONY|1|25|SECURITY_CASE|SECRET').decode()))
rt.private_event_sequence=3
rt.finished=True
rt.error=None

def fresh_snapshot(command,principal=None):
    assert (command,principal)==('SECURITY_REFRESH',8)
    with rt.cv:
        rt.private_event_sequence+=1
        rt.private_colony_events.append((
            rt.private_event_sequence,8,
            base64.b64encode(
                b'COLONY|1|26|SECURITY_CALL_INBOX_RESET').decode()))
        rt.cv.notify_all()

rt.send=fresh_snapshot
def make_handler(principal):
    handler=object.__new__(relay.Handler)
    handler.server=types.SimpleNamespace(runtime=rt)
    handler.wfile=io.BytesIO()
    handler._principal=lambda:principal
    handler.send_response=lambda code:None
    handler.send_header=lambda key,value:None
    handler.end_headers=lambda:None
    return handler

principal={'session_id':'session-8','playerId':8}
handler=make_handler(principal)
handler._security_only_events(principal)
output=handler.wfile.getvalue()
assert output.count(b'event: colony\n')==1,output
assert b'SECURITY_CALL_INBOX_RESET' not in output # SSE is base64 encoded
assert base64.b64encode(b'COLONY|1|26|SECURITY_CALL_INBOX_RESET') in output
assert b'event: frame' not in output and b'event: survey' not in output
assert b'event: complete\n' in output
assert b'PRIVATE|' not in output and b'SECRET' not in output
assert base64.b64encode(b'COLONY|1|25|SECURITY_CALL_INBOX|1001|10|7') not in output

# Expired or logged-out cookies cannot drain the owner-private queue,
# even if the native refresh command was already sent.
revoked=make_handler(None)
revoked._security_only_events(principal)
assert b'event: failed\ndata: Session expired' in revoked.wfile.getvalue()
assert b'event: colony' not in revoked.wfile.getvalue()

# The 4096-row ring rolls forward indefinitely. If a live client misses
# events while it is still connected, the relay must signal a gap, discard
# partial stale receipts and request a second original native refresh.
# The test floods *after* the subscription captures its high-water mark.
rt.private_colony_events=deque(maxlen=4096)
rt.private_event_sequence=0
calls=[0]
def flood_then_recover(command,principal=None):
    assert (command,principal)==('SECURITY_REFRESH',8)
    calls[0]+=1
    with rt.cv:
        if calls[0]==1:
            for i in range(4200):
                rt.private_event_sequence+=1
                rt.private_colony_events.append((
                    rt.private_event_sequence,8 if i%3 else 9,
                    base64.b64encode(
                        f'COLONY|1|25|SECURITY_STATUS|1|1|1|{i%2}|3'
                        .encode()).decode()))
        else:
            rt.private_event_sequence+=1
            rt.private_colony_events.append((
                rt.private_event_sequence,8,
                base64.b64encode(
                    b'COLONY|1|26|SECURITY_CALL_INBOX_RESET').decode()))
        rt.cv.notify_all()
rt.send=flood_then_recover
lagged=make_handler(principal)
lagged._security_only_events(principal)
output=lagged.wfile.getvalue()
assert calls==[2],calls
assert len(rt.private_colony_events)==4096
assert rt.private_event_sequence==4201
assert b'event: resync\ndata: Native security refresh required' in output
assert output.count(b'event: colony\n')==1
assert base64.b64encode(b'COLONY|1|26|SECURITY_CALL_INBOX_RESET') in output
assert base64.b64encode(
    b'COLONY|1|25|SECURITY_STATUS|1|1|1|0|3') not in output
assert base64.b64encode(
    b'COLONY|1|25|SECURITY_STATUS|1|1|1|1|3') not in output
assert b'event: frame' not in output and b'event: survey' not in output
assert b'event: complete\n' in output
print('PASS SHIP-STATUS-07H private relay: reconnect skips stale ring, '
      'account isolation, expiry, rolling overflow and native resnapshot')
