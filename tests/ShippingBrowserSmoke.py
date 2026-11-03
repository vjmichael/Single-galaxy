#!/usr/bin/env python3
"""Optional physical four-owner game -> browser shipment-status acceptance.

Requires playwright Chromium. No real login credentials, Internet or public port.
"""
import argparse
import os
import pathlib
import subprocess
import tempfile
from playwright.sync_api import sync_playwright

ROOT=pathlib.Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--producer',required=True);p.add_argument('--bridge',required=True);p.add_argument('--chromium',default='');args=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='sg-browser-freight-') as tmp:
  temp=pathlib.Path(tmp);db=temp/'world.sqlite';accounts=temp/'accounts.json'
  subprocess.run([args.producer],env={**os.environ,'SG_SHIPPING_STATUS_FIXTURE':str(db),'SG_SHIPPING_RUN_ID':'browser'},check=True,capture_output=True,timeout=30)
  # Provision test credentials using the same production scrypt helper without
  # shipping a password on the native host command line.
  script="""const fs=require('node:fs');const auth=require(process.argv[1]);(async()=>{
  const a=await auth.account('test-source',7,'browser-smoke-account-password');
  fs.writeFileSync(process.argv[2],JSON.stringify({version:1,accounts:[a]}),{mode:0o600});
  })().catch(e=>{console.error(e);process.exit(1)});"""
  subprocess.run(['node','-e',script,str(ROOT/'scripts/shipping_auth.js'),str(accounts)],check=True,capture_output=True,timeout=10)
  srv=subprocess.Popen(['node',str(ROOT/'scripts/shipping_live_server.js'),'--db',str(db),'--accounts',str(accounts),'--bridge',args.bridge],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  try:
   line=srv.stdout.readline().strip();assert line.startswith('SG_SHIPPING_URL='),line
   url=line.split('=',1)[1]
   with sync_playwright() as pw:
    browser=pw.chromium.launch(headless=True,**({"executable_path":args.chromium} if args.chromium else {}))
    page=browser.new_page();page.goto(url,wait_until='load',timeout=15000)
    assert page.locator('#dashboard').is_hidden()
    page.locator('#username').fill('test-source')
    page.locator('#password').fill('browser-smoke-account-password')
    page.get_by_role('button',name='Sign in').click()
    page.get_by_text('Updated 2 shipment(s).').wait_for(timeout=15000)
    assert page.locator('.shipment').count()==2
    assert page.get_by_text('Redirected and settled').count()==1
    assert page.get_by_text('Delivered and settled').count()==1
    assert page.locator('#password').input_value()==''
    page.get_by_role('button',name='Sign out').click()
    assert page.locator('#dashboard').is_hidden()
    browser.close()
   print('Browser: authenticated source sees exactly two real settled shipments, logout PASS')
  finally:
   srv.terminate()
   try:srv.wait(timeout=5)
   except subprocess.TimeoutExpired:srv.kill();srv.wait(timeout=5)
if __name__=='__main__':main()
