// Monitor Buddy simulator, browser build only: the clock's time zone.
//
// Emscripten's localtime() asks the browser, so it knows one zone: the
// computer's. The firmware sets its zone with a POSIX rule (setenv("TZ",
// "CET-1CEST,M3.5.0,M10.5.0/3")), as newlib on the chip reads it, and a
// preview of another city must show that city's time. So this file reads the
// rule itself: std offset [dst [offset] [,start[/time],end[/time]]], names
// plain or in <>, the dates as Mm.w.d, Jn or n.
#ifdef __EMSCRIPTEN__
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

namespace {
struct Change { char kind; int a, b, c; long at; };   // kind 'M' (month, week, day), 'J' (1-365, no leap day) or 'N' (0-365)
struct Zone {
  char std[8] = "UTC", dst[8] = "";
  long stdOff = 0, dstOff = 0;       // seconds east of UTC
  bool hasDst = false;
  Change start = {'M', 3, 2, 0, 7200}, end = {'M', 11, 1, 0, 7200};
};
Zone zone;
char zoneText[80] = "\x01";

const char *name(const char *p, char *out) {
  size_t n = 0;
  if (*p == '<') {
    for (p++; *p && *p != '>'; p++) if (n < 7) out[n++] = *p;
    if (*p == '>') p++;
  } else {
    for (; isalpha((unsigned char)*p); p++) if (n < 7) out[n++] = *p;
  }
  out[n] = 0;
  return p;
}
// [+-]hh[:mm[:ss]] as seconds
const char *span(const char *p, long *out) {
  int sign = 1;
  if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
  long part[3] = {0, 0, 0};
  for (int i = 0; i < 3; i++) {
    part[i] = strtol(p, (char **)&p, 10);
    if (*p != ':') break;
    p++;
  }
  *out = sign * (part[0] * 3600 + part[1] * 60 + part[2]);
  return p;
}
const char *change(const char *p, Change *c) {
  c->at = 7200;
  if (*p == 'M') {
    c->kind = 'M';
    c->a = (int)strtol(p + 1, (char **)&p, 10);
    if (*p == '.') c->b = (int)strtol(p + 1, (char **)&p, 10);
    if (*p == '.') c->c = (int)strtol(p + 1, (char **)&p, 10);
  } else if (*p == 'J') {
    c->kind = 'J';
    c->a = (int)strtol(p + 1, (char **)&p, 10);
  } else {
    c->kind = 'N';
    c->a = (int)strtol(p, (char **)&p, 10);
  }
  if (*p == '/') p = span(p + 1, &c->at);
  return p;
}
void parse() {
  const char *text = getenv("TZ");
  if (!text) text = "UTC0";
  if (strcmp(text, zoneText) == 0) return;
  strncpy(zoneText, text, sizeof(zoneText) - 1);
  zoneText[sizeof(zoneText) - 1] = 0;
  zone = Zone();
  long west = 0;
  const char *p = name(text, zone.std);
  p = span(p, &west);
  zone.stdOff = -west;
  if (!*p) return;
  p = name(p, zone.dst);
  if (!zone.dst[0]) return;
  zone.hasDst = true;
  zone.dstOff = zone.stdOff + 3600;
  if (*p && *p != ',') { p = span(p, &west); zone.dstOff = -west; }
  if (*p == ',') p = change(p + 1, &zone.start);
  if (*p == ',') p = change(p + 1, &zone.end);
}
bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
// days from 1970-01-01 to y-01-01
long yearStart(int y) {
  const long before = y - 1;
  return before * 365 + before / 4 - before / 100 + before / 400 - 719162;
}
// The moment of a change in year y, as seconds of local wall time since 1970.
long long changeAt(const Change &c, int y) {
  static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  long day = 0;
  if (c.kind == 'J') {
    day = c.a - 1;
    if (leap(y) && c.a >= 60) day++;
  } else if (c.kind == 'N') {
    day = c.a;
  } else {
    for (int m = 1; m < c.a; m++) day += days[m - 1] + (m == 2 && leap(y) ? 1 : 0);
    const int inMonth = days[c.a - 1] + (c.a == 2 && leap(y) ? 1 : 0);
    const int first = (int)(((yearStart(y) + day) % 7 + 11) % 7);   // weekday of the 1st, 0 = Sunday (1970-01-01 was a Thursday)
    int d = (c.c - first + 7) % 7 + (c.b - 1) * 7;
    while (d >= inMonth) d -= 7;
    day += d;
  }
  return (long long)(yearStart(y) + day) * 86400 + c.at;
}
// Is daylight time on at UTC second t?
bool summer(long long t) {
  if (!zone.hasDst) return false;
  struct tm g;
  const time_t probe = (time_t)(t + zone.stdOff);
  gmtime_r(&probe, &g);
  const int y = g.tm_year + 1900;
  const long long on = changeAt(zone.start, y) - zone.stdOff, off = changeAt(zone.end, y) - zone.dstOff;
  return on < off ? (t >= on && t < off) : !(t >= off && t < on);   // the second form: the southern half
}
}  // namespace

extern "C" {
void tzset(void) { parse(); }

struct tm *localtime_r(const time_t *t, struct tm *out) {
  parse();
  const bool dst = summer((long long)*t);
  const long off = dst ? zone.dstOff : zone.stdOff;
  const time_t shifted = *t + off;
  gmtime_r(&shifted, out);
  out->tm_isdst = dst ? 1 : 0;
  out->tm_gmtoff = off;
  out->tm_zone = dst ? zone.dst : zone.std;
  return out;
}

struct tm *localtime(const time_t *t) {
  static struct tm out;
  return localtime_r(t, &out);
}

time_t mktime(struct tm *tm) {
  parse();
  const long long wall = (long long)timegm(tm);   // also puts the fields in range
  long long t = wall - zone.stdOff;
  if (zone.hasDst && tm->tm_isdst != 0 && summer(wall - zone.dstOff)) t = wall - zone.dstOff;
  const time_t out = (time_t)t;
  localtime_r(&out, tm);
  return out;
}
}
#endif
