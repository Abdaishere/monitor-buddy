#include "persona.h"
#include <stdio.h>
#include <string.h>

namespace {

bool digits(const char *text, size_t count) {
  for (size_t i = 0; i < count; i++) if (text[i] < '0' || text[i] > '9') return false;
  return true;
}

int number(const char *text, size_t count) {
  int value = 0;
  for (size_t i = 0; i < count; i++) value = value * 10 + (text[i] - '0');
  return value;
}

bool leap(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

bool realDay(int month, int day, bool leapDay) {
  static const uint8_t DAYS[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1 || day > DAYS[month - 1]) return false;
  return leapDay || !(month == 2 && day == 29);
}

// "MM-DD" into month and day; false for anything else.
bool monthDay(const char *text, int &month, int &day) {
  if (!text || strlen(text) != 5 || !digits(text, 2) || text[2] != '-' || !digits(text + 3, 2)) return false;
  month = number(text, 2);
  day = number(text + 3, 2);
  return realDay(month, day, true);
}

// The day a yearly date falls on in `year`: 29 February moves to the 28th.
void inYear(int year, int &month, int &day) {
  if (month == 2 && day == 29 && !leap(year)) day = 28;
}

// The name in capitals, cut to `room` characters.
void capitals(const char *name, char *out, size_t room) {
  size_t n = 0;
  for (; name && name[n] && n < room; n++) out[n] = (name[n] >= 'a' && name[n] <= 'z') ? (char)(name[n] - 32) : name[n];
  out[n] = '\0';
}

void withName(const char *word, const char *name, char *out, size_t size) {
  char caps[13];
  capitals(name, caps, sizeof(caps) - 1);
  if (caps[0]) snprintf(out, size, "%s %s!", word, caps);
  else         snprintf(out, size, "%s!", word);
}

bool celebrates(const PersonaPrefs &prefs, const PersonaNow &now, PersonaStatus &which) {
  if (!now.trusted) return false;
  if (personaIsBirthday(prefs.birthday, now.year, now.month, now.mday)) { which = PERSONA_BIRTHDAY; return true; }
  if (personaYearsSince(prefs.since, now.year, now.month, now.mday) > 0) { which = PERSONA_ANNIVERSARY; return true; }
  return false;
}

}  // namespace

bool personaWorkDay(uint8_t weekend, uint8_t wday) {
  switch (weekend) {
    case WEEKEND_FRI_SAT: return wday != 5 && wday != 6;
    case WEEKEND_FRI:     return wday != 5;
    case WEEKEND_NONE:    return true;
    default:              return wday != 6 && wday != 0;
  }
}

bool personaIsBirthday(const char *mmdd, int year, uint8_t month, uint8_t mday) {
  int m, d;
  if (!monthDay(mmdd, m, d)) return false;
  inYear(year, m, d);
  return m == month && d == mday;
}

int personaYearsSince(const char *ymd, int year, uint8_t month, uint8_t mday) {
  if (!ymd || strlen(ymd) != 10 || !digits(ymd, 4) || ymd[4] != '-') return 0;
  int m, d;
  if (!monthDay(ymd + 5, m, d)) return 0;
  const int from = number(ymd, 4);
  if (!realDay(m, d, leap(from))) return 0;
  inYear(year, m, d);
  if (m != month || d != mday || year <= from) return 0;
  return year - from;
}

void personaGreeting(uint8_t style, const char *custom, const char *name, uint32_t pick, char *out, size_t size) {
  static const char *const MIX[] = {"WHAT'S UP?", "HELLO!", "YES?", "HEY THERE!", "WHO'S THERE?"};
  static const size_t MIX_COUNT = sizeof(MIX) / sizeof(MIX[0]);
  const bool named = name && name[0];
  switch (style) {
    case GREETING_HELLO: withName("HELLO", name, out, size); return;
    case GREETING_YO:    withName("YO", name, out, size); return;
    case GREETING_SALAM: withName("SALAM", name, out, size); return;
    case GREETING_CUSTOM:
      if (custom && custom[0]) { snprintf(out, size, "%.26s", custom); return; }
      break;
    default: break;
  }
  // The mix: the five classics, and with a name one more that uses it.
  const uint32_t count = MIX_COUNT + (named ? 1 : 0);
  const uint32_t at = pick % count;
  if (at < MIX_COUNT) snprintf(out, size, "%s", MIX[at]);
  else                withName("HEY", name, out, size);
}

void personaWelcome(const char *name, char *out, size_t size) {
  char caps[13];
  capitals(name, caps, sizeof(caps) - 1);
  if (caps[0]) snprintf(out, size, "OH! HI %s", caps);
  else         snprintf(out, size, "OH! HI");
}

void personaMeet(const char *buddyName, const char *name, char *me, size_t meSize, char *you, size_t youSize) {
  char caps[13];
  capitals(buddyName, caps, sizeof(caps) - 1);
  if (caps[0]) snprintf(me, meSize, "I'M %s!", caps);
  else         snprintf(me, meSize, "HELLO!");
  capitals(name, caps, sizeof(caps) - 1);
  if (caps[0]) snprintf(you, youSize, "HI %s!", caps);
  else if (youSize) you[0] = '\0';
}

bool personaStretchDue(uint8_t breakMin, uint32_t focusSeconds) {
  if (breakMin == 0) return false;
  const uint32_t every = (uint32_t)breakMin * 60;
  return focusSeconds >= every && focusSeconds % every < 60;
}

PersonaStatus personaNudge(const PersonaPrefs &prefs, const PersonaNow &now) {
  if (!now.trusted) return PERSONA_NONE;
  PersonaStatus party;
  if (celebrates(prefs, now, party) && now.min % 10 == 0 && now.sec < 20) return party;
  if (!personaWorkDay(prefs.weekend, now.wday)) return PERSONA_NONE;
  const bool working = now.hour >= prefs.workStart && now.hour < prefs.workEnd;
  if (working && now.hour == prefs.lunch && now.min == 0) return PERSONA_LUNCH;
  if (prefs.water && working && now.hour != prefs.workStart && now.hour != prefs.lunch && now.min == 0 && now.sec < 30)
    return PERSONA_WATER;
  return PERSONA_NONE;
}

PersonaStatus personaIdle(const PersonaPrefs &prefs, const PersonaNow &now) {
  PersonaStatus party;
  if (celebrates(prefs, now, party)) return party;
  if (prefs.message && prefs.message[0] && now.trusted && now.min % 15 == 7 && now.sec < 12) return PERSONA_MESSAGE;
  if (now.trusted && !personaWorkDay(prefs.weekend, now.wday)) return PERSONA_WEEKEND;
  return PERSONA_NONE;
}

void personaCaption(PersonaStatus status, const PersonaPrefs &prefs, const PersonaNow &now, char *out, size_t size) {
  switch (status) {
    case PERSONA_BIRTHDAY: snprintf(out, size, "HAPPY BIRTHDAY!"); break;
    case PERSONA_ANNIVERSARY: {
      const int years = personaYearsSince(prefs.since, now.year, now.month, now.mday);
      if (years < 1) { if (size) out[0] = '\0'; }   // not the day
      else snprintf(out, size, years == 1 ? "%d YEAR TOGETHER" : "%d YEARS TOGETHER", years);
      break;
    }
    case PERSONA_STRETCH: snprintf(out, size, "STRETCH BREAK?"); break;
    case PERSONA_LUNCH:   snprintf(out, size, "LUNCH TIME?"); break;
    case PERSONA_WATER:   snprintf(out, size, "SIP OF WATER?"); break;
    case PERSONA_MESSAGE: snprintf(out, size, "%.26s", prefs.message ? prefs.message : ""); break;
    case PERSONA_WEEKEND: snprintf(out, size, "WEEKEND"); break;
    default: if (size) out[0] = '\0';
  }
}
