"""The app's live preview (tools/app/sim: the firmware built for a browser by
build_wasm.sh) shows the same pixels as the native simulator: the same script
on both, compared with the same golden pictures. Runs the WebAssembly build
under Node; skipped where Node or the build is missing."""
import json
import os
import shutil
import subprocess

import pytest
from PIL import Image

from conftest import OUT, SIM, odoo_data, pc_state
from mbsim import LINK_KEY, UPDATE_KEY_PEM
from test_pages import PAGES

ROOT = os.path.dirname(os.path.dirname(SIM))
APP_SIM = os.path.join(ROOT, 'tools', 'app', 'sim')
RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'preview_run.js')

pytestmark = pytest.mark.skipif(not shutil.which('node') or not os.path.exists(os.path.join(APP_SIM, 'mbsim.wasm')),
                                reason='needs node and tools/app/sim/mbsim.wasm (tools/sim/build_wasm.sh)')


class Clock:
    """Stands in for a Buddy where conftest's helpers read the time: 0, so their times are offsets from now."""
    true_ms = 0


def offsets(data):
    """odoo_data() made with Clock: its meeting times become {"$now": seconds}, filled in when the step runs."""
    data['m'] = [[name, {'$now': start}, {'$now': end}] for name, start, end in data['m']]
    return data


def preview(steps, out, **boot):
    os.makedirs(out, exist_ok=True)
    boot.setdefault('card', 'ok')
    boot.setdefault('sd', os.path.join(ROOT, 'sdcard', 'gifs'))
    boot.setdefault('files', {'/keys.dat': json.dumps({'link_key': LINK_KEY, 'update_key': UPDATE_KEY_PEM})})
    done = subprocess.run(['node', RUNNER, APP_SIM, out], input=json.dumps({'boot': boot, 'steps': steps}),
                          capture_output=True, text=True, timeout=300)
    assert done.returncode == 0, done.stderr[-2000:]
    return json.loads(done.stdout)


def shot(out, name):
    with open(os.path.join(out, name + '.rgba'), 'rb') as file:
        return Image.frombytes('RGBA', (320, 172), file.read()).convert('RGB')


def test_the_preview_draws_every_page_like_the_simulator(golden):
    """All pages with a day's data, in the browser build: pixel for pixel the golden pictures of test_pages."""
    sessions = [['monitor-buddy', 0, {'$mark': -240}], ['odoo', 1, {'$mark': -95}], ['dotfiles', 2, {'$mark': -1900}]]
    steps = [['run', 12000], ['mark'],
             ['state', pc_state(sessions=sessions)],
             ['odoo', offsets(odoo_data(Clock, meetings=[('Weekly sync', 95, 30), ('Design review', 240, 45)]))],
             ['run', 40000]]
    for page in (5, 7):
        steps += [['page', page], ['run', 4000]]
    steps.append(['state', pc_state(sessions=sessions)])
    for page, name in PAGES.items():
        if name == 'game':
            continue
        steps += [['page', page], ['run', 1000], ['shot', 'page_%02d_%s' % (page, name)], ['run', 1300]]
    out = os.path.join(OUT, 'preview')
    answer = preview(steps, out)
    assert all(step[1] for step in answer['report'] if step[0] in ('state', 'odoo', 'page')), answer['report']
    for page, name in PAGES.items():
        if name != 'game':
            golden('page_%02d_%s' % (page, name), shot(out, 'page_%02d_%s' % (page, name)))


@pytest.mark.parametrize('style', ['classic', 'cat', 'robot', 'minimal'])
def test_the_preview_draws_the_faces_like_the_simulator(golden, style):
    """A setting saved over USB restarts the previewed Buddy too, and its faces match test_faces' golden pictures."""
    steps = [['run', 12000],
             ['set', {'face': style, 'name': 'Sam', 'buddy_name': 'Pixel', 'message': "You've got this"}],
             ['run', 14000], ['get'],
             ['state', pc_state()], ['odoo', odoo_data(Clock, dev=False)], ['run', 40000]]
    names = ['normal', 'happy', 'surprised']
    for number, name in enumerate(names):
        steps += [['page', 0], ['state', pc_state(mood=number)], ['run', 2000], ['shot', 'face_%s_mood_%s' % (style, name)]]
    out = os.path.join(OUT, 'preview')
    answer = preview(steps, out)
    assert answer['restarts'] == 1
    got = [step[1] for step in answer['report'] if step[0] == 'get'][0]
    assert got['config']['face'] == style and got['config']['name'] == 'Sam'
    for name in names:
        golden('face_%s_mood_%s' % (style, name), shot(out, 'face_%s_mood_%s' % (style, name)))
