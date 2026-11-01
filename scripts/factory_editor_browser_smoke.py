"""Optional local Chromium GUI gate for ECON-02B2d4b.

Run only with Playwright installed. Injects local scripts instead of navigating to
file://, because some sandboxed browsers block file/local HTTP navigation.
"""
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parents[1]
VIEWER = ROOT / 'viewer'
FIXTURE = ROOT / 'tests/fixtures/ECON02B2d4b_editor_blueprint.sglayout'

def check(page, element, expected):
    got = page.locator(element).inner_text().strip()
    assert got == expected, (element, got, expected)

with sync_playwright() as pw:
    browser = pw.chromium.launch(headless=True, executable_path='/usr/bin/chromium',
                                 args=['--no-sandbox','--disable-dev-shm-usage'])
    page = browser.new_page(viewport={'width': 1440, 'height': 940}, accept_downloads=True)
    problems = []
    page.on('pageerror',lambda error: problems.append(str(error)))
    html = (VIEWER / 'factory.html').read_text(encoding='utf8').replace(
        '<script src="factory-layout.js"></script><script src="factory.js"></script><script src="factory-live.js"></script>', '')
    page.set_content(html)
    for filename in ['factory-layout.js','factory.js','factory-live.js']:
        page.add_script_tag(content=(VIEWER/filename).read_text(encoding='utf8'))
    page.locator('#example').click()
    for elem, expected in [('#metricDevices','3'),('#metricLinks','2'),('#metricSteps','16'),('#metricModules','11')]:
        check(page,elem,expected)
    with page.expect_download() as download_evt:
        page.locator('#exportNative').click()
    with open(download_evt.value.path(), encoding='utf8') as f:
        assert f.read() == FIXTURE.read_text(encoding='utf8'), 'Browser native export differs from C++ fixture'
    page.locator('[data-tool="wall"]').click()
    box=page.locator('#factoryCanvas').bounding_box()
    assert box and box['width']>100 and box['height']>100
    # Sample camera center is (5,1), zoom 42px per grid cell.
    def px(x,y):return (box['x']+box['width']/2+(x-5)*42,
                        box['y']+box['height']/2+(y-1)*42)
    a=px(1,7);b=px(3,7)
    page.mouse.move(*a);page.mouse.down();page.mouse.move(*b,steps=7);page.mouse.up()
    assert 'Walls: 46' in page.locator('#planDetails').inner_text(), page.locator('#planDetails').inner_text()
    with page.expect_download() as design_evt:
        page.locator('#saveDesign').click()
    path=Path(design_evt.value.path())
    import json
    data=json.loads(path.read_text(encoding='utf8'))
    assert len(data['walls'])==46 and data['canonical']==FIXTURE.read_text(encoding='utf8')
    page.on('dialog',lambda dlg:dlg.accept())
    page.locator('#clear').click()
    check(page,'#metricDevices','0')
    page.locator('#loadFile').set_input_files({'name':'restore.sgfactory','mimeType':'application/json','buffer':path.read_bytes()})
    check(page,'#metricDevices','3')
    assert 'Walls: 46' in page.locator('#planDetails').inner_text()
    page.locator('#loadFile').set_input_files({'name':'native.sglayout','mimeType':'text/plain','buffer':FIXTURE.read_bytes()})
    check(page,'#metricDevices','3')
    assert 'Walls: 0' in page.locator('#planDetails').inner_text()
    assert not problems, f'Page JS errors: {problems}'
    # Screenshot is from the real rendered browser, not mock illustrations.
    page.locator('#example').click()
    output=Path('/mnt/data/factory_editor_b2d4b_preview.png')
    page.screenshot(path=str(output),full_page=True)
    print('Chromium factory editor: place/sample/export/snap/import/restore PASS; screenshot:', output)
    browser.close()
