"""The round of pages as the owner arranges it: "page_order" puts pages first,
"more_pages" switches the work pages off, and a Buddy without either shows the
round it always did."""
from conftest import odoo_data, pc_state

NAMES = {0: 'face', 1: 'clock', 2: 'date', 3: 'weather', 4: 'moon', 5: 'stock', 6: 'github', 7: 'coin', 8: 'abda',
         9: 'odoo', 10: 'prs', 11: 'gifs', 12: 'today', 13: 'game', 14: 'claude'}


def swipe_round(b, limit=20):
    """The pages a finger finds going left from the first one, by name, until the round closes."""
    b.show_page(int(b.status_line().get('app', 0)))
    seen = []
    for _ in range(limit):
        page = b.page()
        if seen and page == seen[0]:
            break
        seen.append(page)
        b.swipe_left()
    return [NAMES[page] for page in seen]


def at_first_page(b):
    """Restart with the day's data in: the Buddy starts on the first page of its round."""
    b.run(14)
    b.state(**pc_state())
    b.odoo(**odoo_data(b, dev=False))
    b.run(40)          # the morning briefing passes
    b.state(**pc_state())
    first = int(b.mb('INFO') and b.status_line().get('app', 0))
    return first


def test_the_round_is_the_usual_one_without_an_order(buddy):
    """No "page_order": the settings say so, and a swipe goes face, clock, date as before."""
    config = buddy.mb('GET')['config']
    assert config['page_order'] == [] and all(config['more_pages'].values())
    at_first_page(buddy)
    buddy.show_page(0)
    assert swipe_round(buddy)[:5] == ['face', 'clock', 'date', 'weather', 'moon']


def test_page_order_changes_the_round(buddy, golden):
    """The clock first, Today second, the face third: the swipes follow, and the rest keep their usual order."""
    answer = buddy.mb_restart('SET', {'config': {'page_order': ['clock', 'today', 'face'], 'pages': {'moon': False},
                                                 'more_pages': {'game': False, 'abda': False}}})
    assert answer['ok'] is True
    at_first_page(buddy)
    config = buddy.mb('GET')['config']
    assert config['page_order'] == ['clock', 'today', 'face'] and config['more_pages']['game'] is False
    buddy.show_page(1)
    buddy.run(1)
    round_ = swipe_round(buddy)
    assert round_[:4] == ['clock', 'today', 'face', 'date'], round_
    assert 'moon' not in round_ and 'game' not in round_ and 'abda' not in round_
    assert round_.index('weather') < round_.index('github') < round_.index('odoo')


def test_a_page_that_is_not_one_is_refused(buddy):
    """A name the firmware does not know: the SET is refused whole and nothing changes."""
    answer = buddy.mb('SET', {'config': {'page_order': ['clock', 'settings'], 'theme': 4}})
    assert answer['ok'] is False and 'page_order[1]' in answer['errors']
    config = buddy.mb('GET')['config']
    assert config['page_order'] == [] and config['theme'] != 4


def test_with_every_first_page_off_the_face_still_leads(buddy):
    """All eight standard pages off: the face page comes back as the first, so the screen is never empty."""
    off = {name: False for name in ('face', 'clock', 'date', 'weather', 'moon', 'stock', 'github', 'coin')}
    buddy.mb_restart('SET', {'config': {'pages': off, 'page_order': ['today', 'odoo']}})
    buddy.run(14)
    round_ = swipe_round(buddy)
    assert 'face' in round_, round_
    at = round_.index('face')      # the round is a ring: read it from the face
    # Today first as asked (Odoo has no data yet and is skipped), then the other pages that have something to show
    assert round_[at:] + round_[:at] == ['face', 'today', 'gifs', 'game', 'abda'], round_
