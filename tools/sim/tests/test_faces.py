"""The face styles (setting "face"), the party face and the wave: every mood
and every status face in each style, compared with golden/face_STYLE_NAME.png.
A contact sheet per style lands in out/faces_STYLE.png."""
import os

import pytest
from PIL import Image, ImageDraw

from conftest import OUT, odoo_data, pc_state

STYLES = ['classic', 'cat', 'robot', 'minimal']
MOODS = ['normal', 'happy', 'surprised', 'sleepy', 'angry', 'sad', 'excited', 'love', 'suspicious']
# STATE demo number -> name (docs/HANDOVER.md §7.4 and §7.12)
STATUSES = {1: 'night', 2: 'lunch', 3: 'away', 4: 'log_hours', 5: 'ci_red', 6: 'locked_in', 7: 'focus', 8: 'focused',
            9: 'claude_needs_you', 10: 'claude_done', 11: 'on_air', 12: 'meeting_soon', 13: 'dizzy', 14: 'unimpressed',
            15: 'greeting', 17: 'birthday', 18: 'anniversary', 19: 'stretch', 20: 'water', 21: 'lunch_time',
            22: 'weekend', 23: 'welcome_back', 24: 'message', 25: 'wave'}


def sheet(style, shots):
    """All pictures of one style on one sheet, six across, each with its name."""
    across, cw, ch = 6, 320 + 12, 172 + 30
    rows = (len(shots) + across - 1) // across
    page = Image.new('RGB', (across * cw + 12, rows * ch + 12), (245, 244, 240))
    draw = ImageDraw.Draw(page)
    for index, (name, image) in enumerate(shots):
        x, y = 12 + (index % across) * cw, 12 + (index // across) * ch
        page.paste(image, (x, y))
        draw.text((x + 2, y + 174), name, fill=(40, 40, 46))
    page.save(os.path.join(OUT, 'faces_%s.png' % style))


@pytest.mark.parametrize('style', STYLES)
def test_every_face_in_the_style(make_buddy, golden, style):
    """Each mood on tap and each status face, drawn in this style; the setting survives the restart."""
    b = make_buddy()
    b.run(12)
    b.mb_restart('SET', {'config': {'face': style, 'name': 'Sam', 'buddy_name': 'Pixel', 'message': "You've got this"}})
    b.run(14)
    assert b.mb('GET')['config']['face'] == style
    b.state(**pc_state())
    b.odoo(**odoo_data(b, dev=False))
    b.run(40)                       # the morning briefing passes
    shots = []
    for number, name in enumerate(MOODS):
        b.show_page(0)
        b.state(**pc_state(mood=number))
        b.run(2)
        shots.append(('mood_' + name, b.shot()))
    for number, name in STATUSES.items():
        b.show_page(0)              # demo mode never holds the face
        b.state(**pc_state(demo=number))
        b.run(2)
        shots.append((name, b.shot()))
    sheet(style, shots)
    for name, image in shots:
        golden('face_%s_%s' % (style, name), image)


def test_an_unknown_style_is_refused(buddy):
    """A face style the firmware does not know: the SET is refused and nothing changes."""
    answer = buddy.mb('SET', {'config': {'face': 'dog'}})
    assert answer['ok'] is False and 'face' in answer['errors']
    assert buddy.mb('GET')['config']['face'] == 'classic'


def test_the_wave_at_the_end_of_setup(buddy, golden):
    """STATE wave over USB, as the installer sends it: the Buddy says its name and waves for 8 s, then goes back."""
    buddy.mb_restart('SET', {'config': {'name': 'Sam', 'buddy_name': 'Pixel'}})
    buddy.run(14)
    mark = buddy.mark()
    buddy.mb('STATE', {'wave': True})
    buddy.show_page(0)
    buddy.run(1)
    assert "mood: status 25 I'M PIXEL!" in buddy.log_since(mark)
    first = buddy.shot()
    buddy.run_ms(350)
    assert buddy.shot().tobytes() != first.tobytes(), 'the hand moves'
    buddy.run(2)
    golden('face_wave_hi_sam', buddy.shot())       # the second line takes its turn
    buddy.run(7)
    assert 'mood: status 0' in buddy.log_since(mark), 'the wave ends by itself'
