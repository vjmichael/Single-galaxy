"""Optional Chromium acceptance of the *real* localhost factory construction UI.
Creates an ephemeral world; never points at a player's real save.
"""
import os, subprocess, tempfile, time, pathlib, select
from playwright.sync_api import sync_playwright
ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = pathlib.Path(os.environ.get('SG_BUILD_DIR', ROOT.parent / 'build'))
with tempfile.TemporaryDirectory(prefix='sg-factory-ui-') as d:
    db = pathlib.Path(d) / 'world.sqlite'
    fixture = subprocess.run([str(BUILD/'galaxy_econ02b2d4c2_world_fixture'),str(db)],capture_output=True,text=True,timeout=20)
    assert fixture.returncode == 0, fixture.stderr
    server=subprocess.Popen(['node',str(ROOT/'scripts/factory_live_server.js'),'--db',str(db), '--bridge',str(BUILD/'galaxy_factory_host_bridge')],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
    try:
        readable, _, _ = select.select([server.stdout], [], [], 10)
        assert readable,'Local server failed to start'
        first=server.stdout.readline().strip()
        assert first.startswith('SG_FACTORY_URL='),first
        url=first.partition('=')[2]
        with sync_playwright() as p:
            browser=p.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox','--disable-dev-shm-usage'])
            page=browser.new_page(viewport={'width':1460,'height':900})
            errors=[]
            page.on('pageerror', lambda e:errors.append(str(e)))
            page.on('dialog', lambda dialog:dialog.accept())
            page.goto(url,wait_until='networkidle')
            page.locator('#liveStatus').get_by_text('Connected',exact=False).wait_for(timeout=5000)
            page.locator('#example').click()
            page.locator('#liveRegister').click()
            page.wait_for_function("document.getElementById('livePlan').textContent.includes('0/16')")
            page.locator('#liveStep').click()
            page.wait_for_function("document.getElementById('livePlan').textContent.includes('1/16')")
            page.locator('#liveRefresh').click()
            page.wait_for_function("document.getElementById('livePlan').textContent.includes('1/16')")
            page.screenshot(path='/mnt/data/factory_live_c2_preview.png',full_page=True)
            page.locator('#liveCancel').click()
            page.wait_for_function("document.getElementById('livePlan').textContent.includes('cancelled')")
            assert not errors,errors
            print('Chromium REAL factory bridge UI: connect, authorize, execute, refresh, cancel PASS')
            browser.close()
    finally:
        server.terminate()
        try:server.wait(timeout=4)
        except subprocess.TimeoutExpired:server.kill()
