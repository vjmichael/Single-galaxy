#!/usr/bin/env python3
"""SHIP-STATUS-07D: real authenticated relay, original native host and SQLite case desk.

This does NOT spawn an unearned foreign NPC colony or invent a criminal case.
Two-owner physical hail/case behavior is covered by native C++ regression.
"""
import argparse
import base64
import http.client
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import time

from SHIPSTATUS06_LiveCyberHostTests import (
    auth_row, free_port, request, wait_for,
)


def claims(db):
    try:
        with sqlite3.connect(db, timeout=2) as con:
            return con.execute(
                "SELECT COUNT(*) FROM sg_disposition02 WHERE kind=1").fetchone()[0]
    except sqlite3.Error:
        return 0


def run(host, relay, bridge):
    with tempfile.TemporaryDirectory(prefix="sg-status07d-real-") as temp:
        root = Path(temp)
        db, accounts = root / "world.sqlite", root / "accounts.json"
        accounts.write_text(json.dumps({"version": 1, "accounts": [
            auth_row("owner", 7, "owner-security-password"),
            auth_row("foreign", 8, "foreign-security-password")]}))
        if os.name != "nt":
            os.chmod(accounts, 0o600)
        port = free_port()
        proc = subprocess.Popen([
            sys.executable, str(relay), "--host-exe", str(host),
            "--colony", "--save", str(db), "--port", str(port),
            "--tick-ms", "25", "--max-ticks", "650",
            "--accounts", str(accounts),
            "--shipping-bridge", str(bridge)],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            def ready():
                if proc.poll() is not None:
                    raise RuntimeError("native relay stopped before login")
                try:
                    return request(port, "GET", "/healthz")[0] == 200
                except OSError:
                    return False
            wait_for(ready, "actual native security relay unavailable")
            code, foreign, cookie = request(port, "POST", "/api/login", {
                "username": "foreign", "password": "foreign-security-password"})
            assert code == 200 and foreign["playerId"] == 8
            cookie = cookie.split(";", 1)[0]
            assert request(port, "POST", "/command", {
                "token": foreign["csrf"],
                "action": "colonySecurityClaim",
                "radius": 3000}, cookie)[0] == 403
            assert request(port, "GET", "/session", cookie=cookie)[0] == 403
            code, owner, cookie = request(port, "POST", "/api/login", {
                "username": "owner", "password": "owner-security-password"})
            assert code == 200 and owner["playerId"] == 7
            cookie = cookie.split(";", 1)[0]
            code, session, _ = request(port, "GET", "/session", cookie=cookie)
            assert code == 200 and "colonySecurityHail" in session["commands"]
            assert "colonySecurityReply" in session["commands"]
            assert request(port, "GET", "/viewer/security-desk.js")[0] == 200
            code, html, _ = request(port, "GET", "/viewer/interactive.html")
            assert code == 200 and b'id="securityCaseList"' in html
            assert b'id="securityHail"' in html
            token = session["token"]
            for body in [
                {"token": token, "action": "colonySecurityClaim",
                 "radius": 3000, "owner": 8},
                {"token": token, "action": "colonySecurityPatrol",
                 "case": 4123456789012345678, "ship": 3},
                {"token": token, "action": "colonySecurityHail",
                 "colony": 3, "visitorOwner": 8},
                {"token": token, "action": "colonySecurityReply",
                 "colony": 3, "notice": 11, "choice": "force"}
            ]:
                assert request(port, "POST", "/command", body, cookie)[0] == 400
            assert request(port, "POST", "/command", {
                "token": token, "action": "colonySecurityClaim",
                "radius": 3000}, cookie, origin=False)[0] == 403
            status, response, _ = request(port, "POST", "/command", {
                "token": token, "action": "colonySecurityRefresh"}, cookie)
            assert status == 202 and response["status"] == "queued_not_committed"
            status, response, _ = request(port, "POST", "/command", {
                "token": token, "action": "colonySecurityClaim",
                "radius": 3000}, cookie)
            assert status == 202 and response["status"] == "queued_not_committed"
            wait_for(lambda: claims(db)==1,
                     "original authenticated native territorial claim not committed")
            status, response, _ = request(port, "POST", "/command", {
                "token": token, "action": "colonySecurityRefresh"}, cookie)
            assert status == 202
            # Read the actual authenticated native SSE stream, not a
            # synthetic local result or a queued HTTP acknowledgement.
            con = http.client.HTTPConnection("127.0.0.1", port, timeout=6)
            telemetry=[]
            try:
                con.request("GET", "/events", headers={"Cookie": cookie})
                stream=con.getresponse()
                assert stream.status == 200
                deadline=time.monotonic()+5
                while time.monotonic()<deadline:
                    line=stream.fp.readline()
                    if not line:
                        break
                    if line.startswith(b"data: "):
                        try:
                            decoded=base64.b64decode(
                                line[6:].strip(), validate=True)
                        except ValueError:
                            continue
                        if decoded.startswith(b"COLONY|1|"):
                            telemetry.append(decoded.decode("ascii"))
                    if any("|SECURITY_RESULT|SECURITY_CLAIM|ACCEPTED|" in v
                           for v in telemetry) and any(
                            "|SECURITY_STATUS|" in v and "|1|0|1" in v
                            for v in telemetry):
                        break
            finally:
                con.close()
            assert any("|SECURITY_RESULT|SECURITY_CLAIM|ACCEPTED|" in v
                       for v in telemetry), telemetry[-15:]
            assert any("|SECURITY_CASES_RESET" in v for v in telemetry)
            assert not any("|SECURITY_CASE|" in v for v in telemetry), (
                "empty genesis invented a cyber offender")
            assert not any("|SECURITY_DESTINATION|" in v for v in telemetry), (
                "unbuilt foreign colony invented at genesis")
        finally:
            proc.terminate()
            try:
                proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill();proc.communicate()
        # Restart the actual native host over the same SQLite world.
        restarted=subprocess.run(
            [str(host),str(db),"20","18","--authenticated-transport"],
            input=("AUTH|7|SECURITY_REFRESH\n"
                   "AUTH|7|SECURITY_CLAIM|3000\n"
                   "AUTH|8|SECURITY_CLAIM|3000\n"
                   "SECURITY_HAIL|2\n"),
            text=True,capture_output=True,timeout=20)
        assert restarted.returncode==0,restarted.stderr[-1800:]
        assert "|SECURITY_STATUS|" in restarted.stderr
        assert "|SECURITY_RESULT|SECURITY_CLAIM|DENIED|" in restarted.stderr
        assert "|ERROR|UNAUTHORIZED_PRINCIPAL" in restarted.stderr
        assert claims(db)==1, "restart duplicated original claim"
        print("PASS SHIP-STATUS-07D real relay and host: owner CSRF, claim,"
              " authenticated case SSE, no invented NPC, restart and spoof denial")


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--host",required=True,type=Path)
    parser.add_argument("--relay",required=True,type=Path)
    parser.add_argument("--bridge",required=True,type=Path)
    args=parser.parse_args()
    run(args.host.resolve(),args.relay.resolve(),args.bridge.resolve())
