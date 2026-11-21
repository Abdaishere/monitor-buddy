#pragma once
// Personal touches: who the Buddy is talking to and when it speaks up. Only
// decisions and caption text live here, in plain C++ with no Arduino headers,
// so every rule is unit-tested on the PC (pio test -e native). src/main.cpp
// draws the result with the faces it already has.
#include <stddef.h>
#include <stdint.h>

static const size_t PERSONA_CAPTION = 27;   // a caption has room for 26 characters

enum PersonaWeekend : uint8_t { WEEKEND_SAT_SUN, WEEKEND_FRI_SAT, WEEKEND_FRI, WEEKEND_NONE };
enum PersonaGreeting : uint8_t { GREETING_MIX, GREETING_HELLO, GREETING_YO, GREETING_SALAM, GREETING_CUSTOM };

// The settings this file reads (the keys of the same names in /config.json).
struct PersonaPrefs {
  const char *name;          // the owner; "" for none
  const char *birthday;      // "MM-DD" or ""
  const char *since;         // "YYYY-MM-DD", the day the Buddy was set up, or ""
  const char *message;       // a line of the owner's own, or ""
  uint8_t breakMin;          // a stretch reminder after every so many minutes of focus; 0 = off
  bool    water;             // a sip reminder on the hour, in work hours
  uint8_t workStart, workEnd;   // hours 0-23 and 1-24, start < end
  uint8_t lunch;             // the hour lunch starts
  uint8_t weekend;           // PersonaWeekend
};

// The local time. wday: 0 = Sunday. trusted: the clock has been set.
struct PersonaNow { int year; uint8_t month, mday, wday, hour, min, sec; bool trusted; };

enum PersonaStatus : uint8_t {
  PERSONA_NONE, PERSONA_BIRTHDAY, PERSONA_ANNIVERSARY, PERSONA_STRETCH, PERSONA_LUNCH,
  PERSONA_WATER, PERSONA_MESSAGE, PERSONA_WEEKEND
};

// Monday to Friday unless the weekend setting says otherwise.
bool personaWorkDay(uint8_t weekend, uint8_t wday);

// True on the day "MM-DD" names; a 29 February birthday is kept on the 28th in
// other years. Anything that is not such a date is never true.
bool personaIsBirthday(const char *mmdd, int year, uint8_t month, uint8_t mday);

// How many whole years ago "YYYY-MM-DD" was, on its anniversary; 0 on any other
// day, in the first year, and for anything that is not such a date. A Buddy set
// up on 29 February has its day on 28 February in other years.
int personaYearsSince(const char *ymd, int year, uint8_t month, uint8_t mday);

// The greeting for a double knock, at most 26 characters. `pick` chooses among
// the lines of GREETING_MIX. The name is shown in capitals; a custom line that
// is empty falls back to the mix.
void personaGreeting(uint8_t style, const char *custom, const char *name, uint32_t pick, char *out, size_t size);

// "OH! HI SAM" when the owner comes back after a long time away.
void personaWelcome(const char *name, char *out, size_t size);

// The wave at the end of setup: "I'M PIXEL!" (or "HELLO!" for a Buddy without
// a name) and "HI SAM!" (or "" without an owner's name).
void personaMeet(const char *buddyName, const char *name, char *me, size_t meSize, char *you, size_t youSize);

// A stretch reminder: the first minute after every breakMin minutes of focus.
bool personaStretchDue(uint8_t breakMin, uint32_t focusSeconds);

// What to say for a short while over the lower-ranked statuses, or NONE:
// the lunch nudge (the first minute of the lunch hour on a work day), the sip
// of water (the first 30 s of each later work hour) and, on the day, 20 s of
// birthday or anniversary every 10 minutes.
PersonaStatus personaNudge(const PersonaPrefs &prefs, const PersonaNow &now);

// What an idle face shows instead of nothing, or NONE: the birthday or
// anniversary all day, the owner's message for 12 s every 15 minutes, and a
// sleepy WEEKEND on days off.
PersonaStatus personaIdle(const PersonaPrefs &prefs, const PersonaNow &now);

// The caption of a status, at most 26 characters ("" for NONE).
void personaCaption(PersonaStatus status, const PersonaPrefs &prefs, const PersonaNow &now, char *out, size_t size);
