Monitor Buddy card
==================

Copy everything in this folder onto a microSD card formatted as FAT32 and put
the card in the Buddy while it is unplugged.

gifs/   Animations for the Buddy's GIF page. It plays one on each visit of the
        page; a tap on the screen steps to the next. All nine were drawn for the
        Buddy by tools/buddy_art.py; full-day, ci-green and focus-done also
        play by themselves (see config/config.h.example).

Add your own GIF, made to fit the 320x172 screen:

    python tools/buddy_gif.py IN.gif sdcard/gifs/NAME.gif

Send it to a Buddy that is running, without taking the card out:

    python tools/buddy_put.py sdcard/gifs/NAME.gif

Without a card the Buddy keeps its animations in its internal storage, which
holds about 800 KB.
