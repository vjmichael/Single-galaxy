#!/usr/bin/env python3
"""SHIP-STATUS-06: real Chromium + authenticated local native game smoke.

Requires Playwright's Chromium and original C++ host + shipping bridge.
No mock endpoints, fabricated foreign NPC, or privileged client issuer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

from playwright.sync_api import sync_playwright


def reserve_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def owner_account(password):
    salt = os.urandom(32)
    digest = hashlib.scrypt(password.encode(), salt=salt, n=16384,
                            r=8, p=1, dklen=64)
    return {"username": "colony-owner", "playerId": 7,
            "salt": salt.hex(), "hash": digest.hex()}


def native_relay(host, relay, bridge, db, accounts, port):
    return subprocess.Popen([
        sys.executable, str(relay), "--host-exe", str(host),
        "--colony", "--save", str(db), "--port", str(port),
        "--tick-ms", "25", "--max-ticks", "650",
        "--accounts", str(accounts), "--shipping-bridge", str(bridge)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def play(browser, url, initial):
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    page.goto(url + "/viewer/interactive.html", wait_until="load")
    page.locator("#colonySignIn").wait_for(state="visible", timeout=15000)
    assert page.locator("#cyberAttack").is_disabled()
    page.locator("#colonyUsername").fill("colony-owner")
    page.locator("#colonyPassword").fill("cyber-browser-fixture-password")
    page.locator("#colonySignIn button[type=submit]").click()
    page.wait_for_function(
        "() => document.getElementById('cyberStatus').textContent.includes('Ship #')",
        timeout=15000)
    assert "No incoming cyber attack" in page.locator("#cyberAlert").inner_text()
    assert page.locator("#cyberAttack").is_disabled()
    assert page.locator("#cyberTarget").locator("option").count() == 1
    if initial:
        assert "no intrusion emitter" in page.locator("#cyberStatus").inner_text()
        for action, args in [
            ("colonyMine", {"amount": 2}),
            ("colonyRefine", {"amount": 2}),
            ("colonyFabricate", {"amount": 1})
        ]:
            queued = page.evaluate(
                "([action, args]) => window.GalaxyColonyCommand(action, args)",
                [action, args])
            assert queued is True, action
            time.sleep(.18)
        page.locator("#cyberFitEmitter").click()
        page.wait_for_function(
            "() => document.getElementById('cyberStatus').textContent.includes('intrusion emitter fitted')",
            timeout=15000)
        assert page.locator("#cyberAttack").is_disabled()
        # Real hardpoint control from the dynamic native telemetry projection.
        emitter_controls = page.locator("#cyberModuleList .controls").filter(
            has_text="Intrusion emitter")
        emitter_controls.wait_for(timeout=8000)
        emitter_controls.get_by_role("button", name="Isolate").click()
        page.wait_for_function(
            "() => document.getElementById('cyberModuleList').textContent.includes('ISOLATED')",
            timeout=10000)
        assert page.locator("#cyberAttack").is_disabled()
        emitter_controls.get_by_role("button", name="Reconnect").click()
        page.wait_for_function(
            "() => !document.getElementById('cyberModuleList').textContent.includes('ISOLATED')",
            timeout=10000)
    else:
        page.wait_for_function(
            "() => document.getElementById('cyberStatus').textContent.includes('intrusion emitter fitted')",
            timeout=12000)
        assert page.locator("#cyberAttack").is_disabled()
    assert "No active Confusion" in page.locator("#cyberDefense").inner_text()
    assert not errors, "browser JavaScript errors: " + repr(errors)
    page.locator("#colonySignOut").click()
    page.locator("#colonySignIn").wait_for(state="visible", timeout=10000)
    assert page.locator("#cyberAttack").is_disabled()
    page.close()


def main(host, relay, bridge, browser_path):
    with tempfile.TemporaryDirectory(prefix="sg-status06-browser-") as temp:
        root = Path(temp)
        db, accounts = root / "world.sqlite", root / "accounts.json"
        accounts.write_text(json.dumps({
            "version": 1, "accounts": [
                owner_account("cyber-browser-fixture-password")]}))
        if os.name != "nt":
            os.chmod(accounts, 0o600)
        with sync_playwright() as pw:
            launch = {"headless": True}
            if browser_path:
                launch["executable_path"] = str(browser_path)
            browser = pw.chromium.launch(**launch)
            try:
                for first in (True, False):
                    port = reserve_port()
                    proc = native_relay(host, relay, bridge, db, accounts, port)
                    try:
                        url = "http://127.0.0.1:" + str(port)
                        import urllib.request
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            if proc.poll() is not None:
                                raise RuntimeError("native relay stopped")
                            try:
                                with urllib.request.urlopen(url + "/healthz", timeout=.5):
                                    break
                            except OSError:
                                time.sleep(.05)
                        else:
                            raise RuntimeError("native relay did not start")
                        play(browser, url, first)
                    finally:
                        proc.terminate()
                        try:
                            proc.communicate(timeout=4)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.communicate()
            finally:
                browser.close()
    print("PASS SHIP-STATUS-06 graphical installed game: authenticated Chromium,"
          " native material-backed emitter, isolation/reconnect, disabled"
          " unobserved target, console checks and SQLite restart")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--relay", required=True, type=Path)
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--chromium", default="")
    args = parser.parse_args()
    main(args.host.resolve(), args.relay.resolve(), args.bridge.resolve(),
         args.chromium)
