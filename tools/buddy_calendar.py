"""Meetings from GNOME's calendar cache: the Google calendars of the account in
GNOME Online Accounts, as Evolution Data Server keeps them on disk. Read only.

    python tools/buddy_calendar.py      # the meetings of the next 24 hours
"""
import glob
import os
import re
import sqlite3
import sys
import time
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

from dateutil.rrule import rrulestr

CACHES = os.path.expanduser('~/.cache/evolution/calendar/*/cache.db')
ACCOUNTS = os.path.expanduser('~/.config/goa-1.0/accounts.conf')
LOCAL = datetime.now().astimezone().tzinfo
MEETING_URL = re.compile(r'https://(?:[\w-]+\.)*(?:meet\.google\.com|zoom\.us|teams\.microsoft\.com|teams\.live\.com)/[^\s"<>\\]*')


def my_addresses():
    try:
        with open(ACCOUNTS) as file:
            return {address.lower() for address in re.findall(r'^Identity=(\S+@\S+)', file.read(), re.M)}
    except OSError:
        return set()


def when(line):
    """The time a DTSTART/DTEND/RECURRENCE-ID/EXDATE line names, or None for a whole day."""
    params, _, value = line.partition(':')
    if 'VALUE=DATE' in params and 'VALUE=DATE-TIME' not in params:
        return None
    zone = re.search(r'TZID=([^;:]+)', params)
    value = value.split(',')[0]
    moment = datetime.strptime(value.rstrip('Z')[:15], '%Y%m%dT%H%M%S')
    if value.endswith('Z'):
        return moment.replace(tzinfo=timezone.utc)
    try:
        return moment.replace(tzinfo=ZoneInfo(zone.group(1)) if zone else LOCAL)
    except (KeyError, ValueError):   # a zone name zoneinfo does not know, as Outlook writes them
        return moment.replace(tzinfo=LOCAL)


def link(event):
    """The event's video call: X-GOOGLE-CONFERENCE, else the first meeting URL in
    LOCATION, DESCRIPTION or URL; '' without one."""
    for line in event.get('X-GOOGLE-CONFERENCE', []):
        if (value := line.partition(':')[2].strip()).startswith('https://'):
            return value
    for name in ('LOCATION', 'DESCRIPTION', 'URL'):
        for line in event.get(name, []):
            if found := MEETING_URL.search(line):
                return found[0].replace('&amp;', '&')   # descriptions are HTML
    return ''


def events(ical):
    """The VEVENTs of an iCalendar text, each a dict of property name to its lines."""
    unfolded = re.sub(r'\r?\n[ \t]', '', ical)
    for block in re.findall(r'BEGIN:VEVENT\r?\n(.*?)END:VEVENT', unfolded, re.S):
        event = {}
        for line in block.splitlines():
            name = re.split(r'[;:]', line, maxsplit=1)[0].upper()
            event.setdefault(name, []).append(line)
        yield event


def meetings(hours=24):
    """[name, start, stop, link], times in UTC seconds, the meetings starting or
    going on from now to `hours` ahead: no whole-day events, none cancelled or
    declined by me."""
    now = datetime.now(timezone.utc)
    until = now + timedelta(hours=hours)
    me = my_addresses()
    found = {}
    for cache in glob.glob(CACHES):
        try:
            with sqlite3.connect(f'file:{cache}?mode=ro', uri=True, timeout=2) as database:
                rows = database.execute('select ECacheOBJ from ECacheObjects').fetchall()
        except sqlite3.Error:
            continue
        for (ical,) in rows:
            moved = {}   # occurrences of a series that were changed one by one
            series = []
            for event in events(ical):
                if 'DTSTART' not in event or any('CANCELLED' in line for line in event.get('STATUS', [])):
                    continue
                if any('PARTSTAT=DECLINED' in line and any(address in line.lower() for address in me)
                       for line in event.get('ATTENDEE', [])):
                    continue
                start = when(event['DTSTART'][0])
                if start is None:
                    continue
                stop = (when(event['DTEND'][0]) if 'DTEND' in event else None) or start + timedelta(hours=1)
                name = event.get('SUMMARY', ['SUMMARY:'])[0].partition(':')[2].replace('\\,', ',').strip()
                if 'RECURRENCE-ID' in event:
                    moved[when(event['RECURRENCE-ID'][0])] = (name, start, stop, link(event))
                else:
                    series.append((event, name, start, stop - start))
            for event, name, start, length in series:
                if 'RRULE' not in event:
                    occurrences = [start]
                else:
                    rule = rrulestr(event['RRULE'][0].partition(':')[2], dtstart=start)
                    occurrences = rule.between(now - length, until, inc=True)
                    skipped = {when(line) for line in event.get('EXDATE', [])}
                    occurrences = [moment for moment in occurrences if moment not in skipped]
                for moment in occurrences:
                    one = moved.pop(moment, None) or (name, moment, moment + length, link(event))
                    if one[2] > now and one[1] < until:
                        found[(one[0], one[1])] = one
            for one in moved.values():   # changed occurrences whose series ends earlier
                if one[2] > now and one[1] < until:
                    found[(one[0], one[1])] = one
    return [[name, int(start.timestamp()), int(stop.timestamp()), url]
            for name, start, stop, url in sorted(found.values(), key=lambda one: one[1])]


if __name__ == '__main__':
    for name, start, stop, url in meetings(int(sys.argv[1]) if len(sys.argv) > 1 else 24):
        print(time.strftime('%a %H:%M', time.localtime(start)), '-', time.strftime('%H:%M', time.localtime(stop)), '|', name, url)
