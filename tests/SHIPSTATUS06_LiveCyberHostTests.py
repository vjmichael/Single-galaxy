#!/usr/bin/env python3
"""SHIP-STATUS-06: real authenticated native host + original Python relay.

This runs actual SQLite-backed native commands and committed SSE, not a stub
or a claim of a packaged Windows installer or manually reviewed browser.
"""
import argparse
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def request(port, method, path, payload=None, cookie=None, origin=True):
    headers = {}
    if cookie:
        headers["Cookie"] = cookie
    if payload is not None:
        headers["Content-Type"] = "application/json"
        if origin:
            headers["Origin"] = f"http://127.0.0.1:{port}"
    con = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    try:
        con.request(method, path, body=(
            json.dumps(payload).encode("utf-8") if payload is not None else None),
            headers=headers)
        response = con.getresponse()
        raw = response.read()
        try:
            body = json.loads(raw)
        except (ValueError, UnicodeDecodeError):
            body = raw
        return response.status, body, response.getheader("Set-Cookie")
    finally:
        con.close()


def current_generation(db):
    try:
        with sqlite3.connect(db, timeout=2) as con:
            value = con.execute(
                "SELECT MAX(generation) FROM snapshots").fetchone()[0]
            return value or 0
    except (sqlite3.Error, OSError):
        return 0


def wait_for(condition, message, timeout=9):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(.02)
    raise AssertionError(message)


def auth_row(username, player, password):
    salt = os.urandom(32)
    digest = hashlib.scrypt(password.encode("utf-8"), salt=salt,
                            n=16384, r=8, p=1, dklen=64)
    return dict(username=username, playerId=player, salt=salt.hex(),
                hash=digest.hex())


def run(host, relay, bridge):
    with tempfile.TemporaryDirectory(prefix="sg-status06-live-") as tmp:
        root = Path(tmp)
        world = root / "world.sqlite"
        accounts = root / "accounts.json"
        port = free_port()
        rows = [auth_row("colony-owner", 7, "owner-fixture-password"),
                auth_row("foreign-visitor", 8, "visitor-fixture-password")]
        accounts.write_text(json.dumps({"version": 1, "accounts": rows}),
                            encoding="utf-8")
        if os.name != "nt":
            os.chmod(accounts, 0o600)
        cmd = [sys.executable, str(relay), "--host-exe", str(host),
               "--colony", "--save", str(world), "--port", str(port),
               "--tick-ms", "20", "--max-ticks", "450",
               "--accounts", str(accounts), "--shipping-bridge", str(bridge)]
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE)
        try:
            def healthy():
                if proc.poll() is not None:
                    raise RuntimeError("real native relay exited before ready")
                try:
                    return request(port, "GET", "/healthz")[0] == 200
                except OSError:
                    return False
            wait_for(healthy, "real relay did not start")
            assert request(port, "GET", "/session")[0] == 403
            assert request(port, "POST", "/command", {
                "token": "fake", "action": "colonyCyberFit",
                "device": "emitter", "install": True})[0] == 403
            foreign_code, foreign_identity, foreign_cookie = request(
                port, "POST", "/api/login", {
                    "username": "foreign-visitor",
                    "password": "visitor-fixture-password"})
            assert foreign_code == 200
            assert foreign_identity["playerId"] == 8
            foreign_cookie = foreign_cookie.split(";", 1)[0]
            assert request(port, "POST", "/command", {
                "token": foreign_identity["csrf"], "action": "colonyCyberAttack",
                "target": 1}, foreign_cookie)[0] == 403

            login, principal, cookie = request(port, "POST", "/api/login", {
                "username": "colony-owner",
                "password": "owner-fixture-password"})
            assert login == 200 and principal["playerId"] == 7
            cookie = cookie.split(";", 1)[0]
            code, session, _ = request(port, "GET", "/session", cookie=cookie)
            assert code == 200 and "colonyCyberIsolate" in session["commands"]
            csrf = session["token"]
            code, html, _ = request(port, "GET", "/viewer/interactive.html")
            assert code == 200 and b'id="cyberDefense"' in html
            assert b'id="cyberAlert"' in html
            code, js, _ = request(port, "GET", "/viewer/cyber-controls.js")
            assert code == 200 and b"CYBER_INCOMING" in js
            assert request(port, "POST", "/command", {
                "token": csrf, "action": "colonyCyberAttack",
                "target": 999, "issuer": 7}, cookie)[0] == 400
            assert request(port, "POST", "/command", {
                "token": csrf, "action": "colonyCyberAttack",
                "target": 999}, cookie, origin=False)[0] == 403
            assert request(port, "POST", "/command", {
                "token": csrf, "action": "colonyCyberIsolate",
                "slot": True, "enabled": True}, cookie)[0] == 400

            # Manufacture, install and operate genuine material-backed
            # hardware. Commit every command before issuing its successor.
            for action, args in [
                ("colonyMine", {"amount": 2}),
                ("colonyRefine", {"amount": 2}),
                ("colonyFabricate", {"amount": 1}),
                ("colonyCyberFit", {"device": "emitter", "install": True}),
                ("colonyCyberPower", {"slot": 1, "enabled": False}),
                ("colonyCyberPower", {"slot": 1, "enabled": True}),
                ("colonyCyberIsolate", {"slot": 1, "enabled": True}),
                ("colonyCyberIsolate", {"slot": 1, "enabled": False}),
                ("colonyCyberAttack", {"target": 999})
            ]:
                before = current_generation(world)
                status, reply, _ = request(port, "POST", "/command", {
                    "token": csrf, "action": action, **args}, cookie)
                assert status == 202 and reply["status"] == "queued_not_committed", (
                    action, status, reply)
                wait_for(lambda: current_generation(world) >= before + 2,
                         "native world did not commit " + action, 5)

            # Read real, owner-session-scoped SSE until the finite host exits.
            # A queued HTTP 202 alone is never counted as native success.
            con = http.client.HTTPConnection("127.0.0.1", port, timeout=25)
            try:
                con.request("GET", "/events", headers={"Cookie": cookie})
                response = con.getresponse()
                assert response.status == 200
                raw = response.read()
            finally:
                con.close()
            lines = []
            for line in raw.splitlines():
                if line.startswith(b"data: "):
                    try:
                        value = base64.b64decode(line[6:], validate=True)
                    except ValueError:
                        continue
                    if value.startswith(b"COLONY|1|"):
                        lines.append(value.decode("ascii"))
            assert any(re.search(r"\|CYBER_RESULT\|[0-9]+\|1\|APPLIED$", x)
                       for x in lines), "physical emitter fitting not committed"
            assert any(re.search(r"\|CYBER_RESULT\|[0-9]+\|2\|APPLIED$", x)
                       for x in lines), "module power/isolation not committed"
            assert any(re.search(r"\|CYBER_RESULT\|[0-9]+\|3\|INVALID_TARGET$", x)
                       for x in lines), "fake physical target was not refused"
            assert any(re.search(r"\|CYBER_STATUS\|[0-9]+\|1\|", x)
                       for x in lines), "installed hardware not reported"
            assert any("|CYBER_DEFENSE|" in x for x in lines)
            assert not any("|CYBER_INCOMING|" in x for x in lines), (
                "production genesis invented a hostile NPC cyber attack")
            assert request(port, "GET", "/api/session", cookie=cookie)[0] == 200
        finally:
            proc.terminate()
            try:
                proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.communicate()
        # Original saved world must survive relay restart without replaying
        # pending UI acknowledgments as a new physical item or combat effect.
        check = subprocess.run(
            [str(host), str(world), "20", "8", "--authenticated-transport"],
            input="AUTH|7|FLEET_STATUS\n", text=True, capture_output=True,
            timeout=15)
        assert check.returncode == 0, check.stderr[-1000:]
        assert "|RESUMED" in check.stderr
        assert re.search(r"\|CYBER_STATUS\|[0-9]+\|1\|",
                         check.stderr), "persisted emitter lost on restart"
        print("PASS SHIP-STATUS-06: authenticated real host/relay, physical fitting,"
              " power/isolation, false-target denial, committed defense SSE and restart")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--relay", required=True, type=Path)
    parser.add_argument("--bridge", required=True, type=Path)
    args = parser.parse_args()
    run(args.host.resolve(), args.relay.resolve(), args.bridge.resolve())
