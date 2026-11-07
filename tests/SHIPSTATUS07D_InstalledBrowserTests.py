#!/usr/bin/env python3
"""SHIP-STATUS-07D: actual authenticated Chromium security desk + SQLite restart.

Real native host, original relay, HTTP login/session, rendered DOM and case
readout. Does not invent NPC enemy colonies or claim a verified crime.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.request

from playwright.sync_api import sync_playwright
from SHIPSTATUS06_InstalledBrowserTests import (
    native_relay,owner_account,reserve_port,
)


def play(browser, url, first):
    page=browser.new_page()
    errors=[]
    page.on("pageerror",lambda error: errors.append(str(error)))
    page.goto(url+"/viewer/interactive.html",wait_until="load")
    page.locator("#colonySignIn").wait_for(state="visible",timeout=15000)
    page.locator("#colonyUsername").fill("colony-owner")
    page.locator("#colonyPassword").fill("cyber-browser-fixture-password")
    page.locator("#colonySignIn button[type=submit]").click()
    page.wait_for_function(
        "() => document.getElementById('securityDeskStatus')"
        ".textContent.includes('Home colony #')",
        timeout=15000)
    assert page.locator("#securityCaseList").inner_text().startswith(
        "No witnessed cyber incidents")
    assert page.locator("#securityHail").is_disabled()
    assert page.locator("#securityAcknowledge").is_disabled()
    assert page.locator("#securityPatrol").is_disabled()
    assert page.locator("#securityCallShip").is_disabled()
    assert page.locator("#securityIdentifyCaller").is_disabled()
    assert page.locator("#securityDeclineCaller").is_disabled()
    assert "No communications abuse penalty" in page.locator(
        "#securityCommsPenalty").inner_text()
    if first:
        assert not page.locator("#securityClaim").is_disabled()
        page.locator("#securityClaimRadius").fill("3000")
        page.locator("#securityClaim").click()
        page.wait_for_function(
            "() => document.getElementById('securityDeskStatus')"
            ".textContent.includes('territory claimed')",
            timeout=12000)
        assert page.locator("#securityClaim").is_disabled()
        assert "native ACCEPTED" in page.locator(
            "#securityDeskFeedback").inner_text()
    else:
        assert "territory claimed" in page.locator(
            "#securityDeskStatus").inner_text()
        assert page.locator("#securityClaim").is_disabled()
    page.locator("#securityRefresh").click()
    page.wait_for_function(
        "() => document.getElementById('securityDeskStatus')"
        ".textContent.includes('territory claimed')",
        timeout=10000)
    assert page.locator("#securityContact").locator("option").count()==1
    assert page.locator("#securityDestination").locator("option").count()==1
    assert page.locator("#securityNotice").locator("option").count()==1
    assert page.locator("#securityCallInbox").locator("option").count()==1
    assert page.locator("#securityCallShip").is_disabled()
    assert page.locator("#securityCommsComplaints").inner_text().startswith(
        "No communications abuse complaints")
    assert not errors,"actual Chromium security errors: "+repr(errors)
    page.locator("#colonySignOut").click()
    page.locator("#colonySignIn").wait_for(state="visible",timeout=10000)
    assert "No witnessed cyber incidents" in page.locator(
        "#securityCaseList").inner_text()
    assert page.locator("#securityClaim").is_disabled()
    assert page.locator("#securityCallShip").is_disabled()
    assert "No communications abuse penalty" in page.locator(
        "#securityCommsPenalty").inner_text()
    page.close()


def main(host,relay,bridge):
    with tempfile.TemporaryDirectory(prefix="sg-status07d-browser-") as temp:
        root=Path(temp)
        db,accounts=root/"world.sqlite",root/"accounts.json"
        accounts.write_text(json.dumps({
            "version":1,"accounts":[
                owner_account("cyber-browser-fixture-password")]}))
        if os.name!="nt":os.chmod(accounts,0o600)
        with sync_playwright() as playwright:
            browser=playwright.chromium.launch(headless=True)
            try:
                for initial in (True,False):
                    port=reserve_port()
                    proc=native_relay(host,relay,bridge,db,accounts,port)
                    try:
                        url=f"http://127.0.0.1:{port}"
                        deadline=time.monotonic()+10
                        while time.monotonic()<deadline:
                            if proc.poll() is not None:
                                raise RuntimeError("native relay exited")
                            try:
                                with urllib.request.urlopen(
                                    url+"/healthz",timeout=.5):
                                    break
                            except OSError:
                                time.sleep(.05)
                        else:raise RuntimeError("security relay startup timeout")
                        play(browser,url,initial)
                    finally:
                        proc.terminate()
                        try:proc.communicate(timeout=5)
                        except subprocess.TimeoutExpired:
                            proc.kill();proc.communicate()
            finally:browser.close()
    print("PASS SHIP-STATUS-07D installed Chromium: authenticated case desk,"
          " physical claim, no invented foreign ships, voluntary hail gate,"
          " session logout privacy and SQLite restart")


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--host",required=True,type=Path)
    parser.add_argument("--relay",required=True,type=Path)
    parser.add_argument("--bridge",required=True,type=Path)
    args=parser.parse_args()
    main(args.host.resolve(),args.relay.resolve(),args.bridge.resolve())
