"""Fixtures for the simulator scenarios.

`buddy` is a Buddy on Wi-Fi with the mock internet, booted on Monday
2026-01-05 at 08:00 (Dubai). `make_buddy(**options)` builds any other.
`golden(name, image)` compares a screenshot with tools/sim/golden/NAME.png to
the pixel; MBSIM_UPDATE_GOLDEN=1 writes the pictures instead.
"""
import json
import os
import sys
import time

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.dirname(HERE)
sys.path.insert(0, SIM)
sys.path.insert(0, os.path.join(SIM, '..'))

from mbsim import BINARY, NATIVE, Buddy, diff  # noqa: E402
from mock_internet import MockInternet  # noqa: E402

GOLDEN = os.path.join(SIM, 'golden')
OUT = os.environ.get('MBSIM_OUT', os.path.join(SIM, 'out'))
UPDATE = os.environ.get('MBSIM_UPDATE_GOLDEN') == '1'


def pytest_configure(config):
    if not os.path.exists(BINARY):
        raise pytest.UsageError('no simulator at %s: run `make` in tools/sim first' % BINARY)
    os.makedirs(OUT, exist_ok=True)
    config._mbsim_results = []
    config.addinivalue_line('markers', 'workday: the 24-hour day (some minutes); leave it out with -m "not workday"')


def pytest_collection_modifyitems(config, items):
    # the 24-hour day takes minutes: on another panel size it runs only with MBSIM_WORKDAY=1
    if NATIVE or os.environ.get('MBSIM_WORKDAY') == '1':
        return
    for item in items:
        if item.get_closest_marker('workday'):
            item.add_marker(pytest.mark.skip(reason='the whole workday is skipped on other panel sizes (MBSIM_WORKDAY=1 runs it)'))


@pytest.fixture
def internet():
    mock = MockInternet().start()
    yield mock
    mock.stop()


@pytest.fixture
def make_buddy(internet):
    made = []

    def make(**options):
        options.setdefault('internet', internet)
        buddy = Buddy(**options)
        made.append(buddy)
        return buddy

    yield make
    for buddy in made:
        buddy.close()


@pytest.fixture
def buddy(make_buddy):
    """On Wi-Fi, clock set, past the boot screens: 12 s after power-on."""
    b = make_buddy()
    b.run(12)
    return b


@pytest.fixture
def golden(request):
    def check(name, image, max_diff=0):
        path = os.path.join(GOLDEN, name + '.png')
        os.makedirs(os.path.join(OUT, 'shots'), exist_ok=True)
        image.save(os.path.join(OUT, 'shots', name + '.png'))
        if not NATIVE:      # another panel size: the pictures are for looking at, only 320x172 has goldens
            return
        if UPDATE or not os.path.exists(path):
            if not UPDATE and os.environ.get('CI'):
                pytest.fail('no golden picture %s.png; make it with MBSIM_UPDATE_GOLDEN=1' % name)
            image.save(path)
            return
        from PIL import Image
        want = Image.open(path).convert('RGB')
        count, where = diff(want, image)
        if count > max_diff:
            os.makedirs(os.path.join(OUT, 'diffs'), exist_ok=True)
            image.save(os.path.join(OUT, 'diffs', name + '.got.png'))
            want.save(os.path.join(OUT, 'diffs', name + '.want.png'))
            if where:
                where.save(os.path.join(OUT, 'diffs', name + '.diff.png'))
            pytest.fail('%s: %d pixels differ from golden/%s.png (allowed %d); see out/diffs/' % (name, count, name, max_diff))
    return check


# ── the report: one line per scenario ─────────────────────────
@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when == 'call' or (report.when == 'setup' and report.outcome != 'passed'):
        doc = (item.function.__doc__ or '').strip().split('\n')[0]
        item.config._mbsim_results.append({
            'id': item.nodeid.split('tests/')[-1], 'what': doc, 'outcome': report.outcome,
            'seconds': round(report.duration, 2),
            'why': str(report.longrepr).strip().split('\n')[-1][:400] if report.outcome == 'failed' else
                   (str(report.longrepr[2]) if report.outcome == 'skipped' and isinstance(report.longrepr, tuple) else ''),
            'xfail': hasattr(report, 'wasxfail'), 'xfail_why': getattr(report, 'wasxfail', ''),
        })


def pytest_sessionfinish(session, exitstatus):
    results = session.config._mbsim_results
    if not results:
        return
    with open(os.path.join(OUT, 'results.json'), 'w') as file:
        json.dump({'when': time.strftime('%Y-%m-%d %H:%M:%S'), 'exit': int(exitstatus), 'results': results}, file, indent=1)


# ── a working day's data, as the laptop sends it ──────────────
def odoo_data(buddy, hours=2.5, before=7.75, meetings=(), dev=True, **more):
    """An Odoo packet (docs/HANDOVER.md §3.5). Meetings are (name, minutes from now, minutes long)."""
    now = buddy.true_ms // 1000
    data = {'h': hours, 'hb': before, 'bw': 5, 'y': ['Fix the invoice export', 'Review stock moves'], 'o': 7, 'r': 2,
            'd': 1 if dev else 0, 'a': 3,
            't': [[48211, 'Invoice export drops the tax line', 'Development (Team)' if dev else 'Code Review'],
                  [48190, 'Stock move rounding', 'Code Review'], [47999, 'Upgrade script for 19.0', 'To Do']],
            'm': [[name, now + start * 60, now + (start + length) * 60] for name, start, length in meetings]}
    data.update(more)
    return data


PC = {'locked': False, 'mic': False, 'prs': 3, 'prs_failing': 0, 'reviews': 2, 'changes': 1, 'prs_merged': 2,
      'prs_reviewed': 4, 'claude': 0, 'at': '',
      'pr_list': [{'t': '#4821 Fix the invoice export', 's': 1, 'r': 2}, {'t': '#4790 Stock move rounding', 's': 3, 'r': 0},
                  {'t': '#4702 Upgrade script for 19.0', 's': 1, 'r': 1}],
      'sessions': [], 'use': [34, 61], 'use_reset': ['13:10', 'Sat'], 'in': '08:01', 'out': '', 'yin': '08:12', 'yout': '17:48'}


def pc_state(**changes):
    state = dict(PC)
    state.update(changes)
    return state


@pytest.fixture
def at_work(buddy):
    """A Buddy in the middle of a quiet morning: the laptop is unlocked, the briefing is over."""
    assert buddy.state(**pc_state())
    assert buddy.odoo(**odoo_data(buddy, dev=False))
    buddy.run(40)          # the Yesterday and Today pages of the morning briefing
    buddy.state(**pc_state())
    return buddy
