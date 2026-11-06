#!/usr/bin/env python3
"""SHIP-STATUS-05: private relay command schema/identity gates, no live server."""
import importlib.util
import io
import json
import pathlib
import types

import_path = pathlib.Path(__file__).resolve().parents[1] / "live" / "interactive_relay.py"
spec = importlib.util.spec_from_file_location("single_galaxy_cyber_relay", import_path)
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)


class Runtime:
    token = "verified-local-session"
    sent = []

    def send(self, command, principal=None):
        self.sent.append((command, principal))


def submit(action, fields=None, token=Runtime.token, origin=True, colony_mode=True):
    doc = dict(token=token, action=action, **(fields or {}))
    request = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    assert len(request) < 512
    server = types.SimpleNamespace(
        accounts=None, colony_mode=colony_mode, qa_mode=False, server_port=8766,
        runtime=Runtime()
    )
    handler = object.__new__(relay.Handler)
    handler.server = server
    handler.path = "/command"
    handler.rfile = io.BytesIO(request)
    handler.headers = {
        "Origin": "http://127.0.0.1:8766" if origin else "http://malicious.example",
        "Content-Type": "application/json",
        "Content-Length": str(len(request)),
    }
    responses = []
    handler._host_ok = lambda: True
    handler._principal = lambda: {"playerId": 7, "csrf": Runtime.token}
    handler._json = lambda status, body: responses.append((status, body))
    Runtime.sent = []
    handler.do_POST()
    assert len(responses) == 1, responses
    return responses[0][0], tuple(Runtime.sent)


for action, payload, expected in (
    ("colonyCyberFit", {"device": "emitter", "install": True}, "CYBER_FIT|3"),
    ("colonyCyberFit", {"device": "firewall", "install": False}, "CYBER_FIT|-4"),
    ("colonyCyberPower", {"slot": 1, "enabled": True}, "CYBER_POWER|1|1"),
    ("colonyCyberIsolate", {"slot": 0, "enabled": False}, "CYBER_ISOLATE|0|0"),
    ("colonyCyberAttack", {"target": 22}, "CYBER_ATTACK|22"),
):
    code, submitted = submit(action, payload)
    assert code == 202 and submitted == ((expected, None),), (action, code, submitted)

for action, payload in (
    ("colonyCyberFit", {"device": "energy siphon", "install": True}),
    ("colonyCyberFit", {"device": "emitter", "install": 1}),
    ("colonyCyberPower", {"slot": True, "enabled": True}),
    ("colonyCyberPower", {"slot": 2, "enabled": True}),
    ("colonyCyberIsolate", {"slot": 0, "enabled": "yes"}),
    ("colonyCyberAttack", {"target": "22"}),
    ("colonyCyberAttack", {"target": 0}),
    ("colonyCyberAttack", {"target": 22, "issuer": 8}),
    ("colonyCyberAttack", {"target": 22, "command_id": 123}),
):
    code, submitted = submit(action, payload)
    assert code == 400 and not submitted, (action, code, submitted)

for action, fields, opt in (
    ("colonyCyberAttack", {"target": 22}, {"token": "wrong-session"}),
    ("colonyCyberAttack", {"target": 22}, {"origin": False}),
):
    code, submitted = submit(action, fields, **opt)
    assert code == 403 and not submitted, (code, submitted)

code, submitted = submit("colonyCyberAttack", {"target": 22}, colony_mode=False)
assert code == 403 and not submitted

print("PASS SHIP-STATUS-05: locally validated cyber actions, strict relay fields, CSRF/session, and no caller-supplied authority")
