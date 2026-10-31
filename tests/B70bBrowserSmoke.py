"""Optional real Chromium smoke: interactive colony UI, live resource loop and visual snapshot."""
import argparse
import asyncio
import shutil
import socket
import subprocess
import sys
import tempfile
from pathlib import Path
from playwright.async_api import async_playwright

async def check(host,relay,shot):
  with tempfile.TemporaryDirectory(prefix='sg_b70b_browser_') as tmp:
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    cmd=[sys.executable,str(relay),'--host-exe',str(host),'--colony','--save',str(Path(tmp)/'game.sqlite'),
        '--port',str(port),'--tick-ms','100','--max-ticks','500']
    proc=subprocess.Popen(cmd,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
      async with async_playwright() as pw:
        browser=await pw.chromium.launch(headless=True,executable_path='/usr/bin/chromium',
            args=['--no-sandbox','--disable-dev-shm-usage','--disable-gpu'])
        try:
          page=await browser.new_page(viewport={'width':1440,'height':1050})
          errors=[]
          page.on('pageerror',lambda e:errors.append(str(e)))
          for attempt in range(30):
            try:
              await page.goto(f'http://127.0.0.1:{port}/viewer/interactive.html',wait_until='domcontentloaded',timeout=3000)
              break
            except Exception:
              await asyncio.sleep(.05)
          await page.locator('#colonyPanel').wait_for(state='visible',timeout=8000)
          await page.wait_for_function("document.getElementById('colonyCrew').textContent==='24'",timeout=8000)
          assert await page.locator('#interactiveControls').is_hidden()
          assert await page.locator('#surveyPanel').is_hidden()
          assert await page.locator('#colonyGeology .entry').count()==3
          assert await page.locator('#colonyOutposts').text_content()=='0'
          for element in ('colonyMine','colonyRefine','colonyFabricate','colonyBuild'):
            await page.locator('#'+element).click()
            # Wait for action to finish in order before dispatching the next one.
            if element=='colonyMine':await page.wait_for_function("document.getElementById('colonyRaw').textContent==='8'",timeout=6000)
            if element=='colonyRefine':await page.wait_for_function("document.getElementById('colonyRefined').textContent==='8'",timeout=6000)
            if element=='colonyFabricate':await page.wait_for_function("document.getElementById('colonyModules').textContent==='4'",timeout=6000)
            if element=='colonyBuild':await page.wait_for_function("document.getElementById('colonyOutposts').textContent==='1'",timeout=6000)
          await page.locator('#colonyScan').click()
          await page.wait_for_timeout(300)
          assert not errors,errors
          assert 'Invalid' not in (await page.locator('#colonyEvents').text_content())
          if shot:
            shot.parent.mkdir(parents=True,exist_ok=True)
            await page.screenshot(path=str(shot),full_page=True,timeout=10000)
          print('PASS B7.0b actual Chromium: scene, 3 mineral bodies, 24 crew, mine/refine/fabricate/build, live outpost, no JS exceptions')
        finally:await browser.close()
    finally:
      proc.terminate()
      try:proc.wait(timeout=5)
      except subprocess.TimeoutExpired:proc.kill();proc.wait()
if __name__=='__main__':
  p=argparse.ArgumentParser();p.add_argument('--host',required=True,type=Path);p.add_argument('--relay',required=True,type=Path);p.add_argument('--shot',type=Path)
  a=p.parse_args();asyncio.run(check(a.host.resolve(),a.relay.resolve(),a.shot))
