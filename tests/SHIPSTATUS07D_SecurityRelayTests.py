#!/usr/bin/env python3
"""SHIP-STATUS-07D: authenticated exact-shape case + hail HTTP relay gates."""
import importlib.util
import io
import json
from pathlib import Path
import types

source = Path(__file__).resolve().parents[1] / "live" / "interactive_relay.py"
spec = importlib.util.spec_from_file_location("sg_security_relay", source)
relay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(relay)


class Runtime:
    token = "trusted-owner-csrf"
    sent = []

    def send(self, command, principal=None):
        self.sent.append((command, principal))


def submit(action, payload=None, principal=7, origin=True,
           csrf=Runtime.token, colony=True):
    body = json.dumps({"token": csrf, "action": action,
                       **(payload or {})}, separators=(",", ":")).encode()
    assert len(body) < 512
    server = types.SimpleNamespace(accounts=object(), colony_mode=colony,
                                   qa_mode=False, server_port=8766,
                                   runtime=Runtime())
    handler = object.__new__(relay.Handler)
    handler.server = server
    handler.path = "/command"
    handler.rfile = io.BytesIO(body)
    handler.headers = {
        "Origin": "http://127.0.0.1:8766" if origin else "http://foreign.test",
        "Content-Type": "application/json",
        "Content-Length": str(len(body))
    }
    handler._host_ok = lambda: True
    handler._principal = lambda: (
        {"playerId": principal, "csrf": Runtime.token}
        if principal is not None else None)
    responses = []
    handler._json = lambda status, result: responses.append((status, result))
    Runtime.sent = []
    handler.do_POST()
    assert len(responses) == 1, responses
    return responses[0][0], list(Runtime.sent)


for action, args, line in [
    ("colonySecurityRefresh", {}, "SECURITY_REFRESH"),
    ("colonySecurityClaim", {"radius": 3000}, "SECURITY_CLAIM|3000"),
    ("colonySecurityWarn", {"ship": 888}, "SECURITY_WARN|888"),
    ("colonySecurityHail", {"colony": 345}, "SECURITY_HAIL|345"),
    ("colonySecurityReply",
     {"colony": 345, "notice": 9000000001, "choice": "acknowledge"},
     "SECURITY_REPLY|345|9000000001|ACK"),
    ("colonySecurityReply",
     {"colony": 345, "notice": 9000000001, "choice": "refuse"},
     "SECURITY_REPLY|345|9000000001|REFUSE"),
    ("colonySecurityPatrol",
     {"case": "4123456789012345678", "ship": 88},
     "SECURITY_PATROL|4123456789012345678|88")
]:
    status, sent = submit(action, args)
    assert status == 202 and sent == [(line, 7)], (action, status, sent)

for action, args in [
    ("colonySecurityRefresh", {"owner": 7}),
    ("colonySecurityClaim", {"radius": True}),
    ("colonySecurityClaim", {"radius": 5001}),
    ("colonySecurityWarn", {"ship": "888"}),
    ("colonySecurityWarn", {"ship": 0}),
    ("colonySecurityHail", {"colony": 1, "pilotedShip": 999}),
    ("colonySecurityHail", {"colony": 0}),
    ("colonySecurityReply",
     {"colony": 3, "notice": 4, "choice": "silently_identify"}),
    ("colonySecurityReply",
     {"colony": 3, "notice": 4, "choice": "acknowledge",
      "issuer": 8}),
    ("colonySecurityReply",
     {"colony": 3, "notice": 4.5, "choice": "refuse"}),
    ("colonySecurityPatrol", {"case": 4123456789012345678, "ship": 88}),
    ("colonySecurityPatrol", {"case": "000001", "ship": 88}),
    ("colonySecurityPatrol", {"case": "4611686018427387904", "ship": 88}),
    ("colonySecurityPatrol", {"case": "1e10", "ship": 88}),
    ("colonySecurityPatrol", {"case": "1", "ship": 88,
                              "attackAuthorized": True})
]:
    status, sent = submit(action, args)
    assert status == 400 and not sent, (action, status, sent)

for options in [
    {"principal": None},
    {"principal": 8},
    {"origin": False},
    {"csrf": "forged-token"},
    {"colony": False}
]:
    status, sent = submit("colonySecurityHail", {"colony": 2}, **options)
    assert status == 403 and not sent, (options, status, sent)

print("PASS SHIP-STATUS-07D: session-owned security claim, hailed hull identity,"
      " exact notice replies, 62-bit case IDs, CSRF and reject forged authority")
