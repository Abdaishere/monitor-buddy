#pragma once
// Stream scanners for the GitHub page's public, token-free data. Pure C++ with
// no Arduino headers, so they are unit-tested on the PC (pio test -e native).
//
// Both read a response a chunk at a time and keep only counters, because the
// bodies (170 kB of repo JSON, 220 kB of contribution HTML) do not fit in the
// ESP32-C6's RAM next to a TLS session.
#include <stddef.h>
#include <stdint.h>

namespace ghscan {

// Finds a fixed word in a byte stream, across chunk boundaries.
struct Needle {
  const char *text;
  uint8_t     at = 0;
  explicit Needle(const char *t) : text(t) {}
  // True on the byte that completes the word.
  bool step(char c);
  void reset() { at = 0; }
};

// Sums "stargazers_count" over GET /users/<name>/repos.
struct StarScan {
  long stars = 0;
  int  repos = 0;       // values seen, one per repo
  void feed(const uint8_t *data, size_t len);

 private:
  Needle key{"\"stargazers_count\":"};
  bool   inNumber = false, sawDigit = false;
  long   value = 0;
};

// Reads the contribution calendar from github.com/users/<name>/contributions.
// Each day is a <td data-date="YYYY-MM-DD"> followed by a <tool-tip> whose text
// starts with the count ("3 contributions on ...") or with "No".
struct ContribScan {
  static const int DAYS = 400;   // the calendar holds 53 weeks

  void feed(const uint8_t *data, size_t len);

  int  days() const { return seen; }
  // Contributions on the newest day in the calendar. GitHub draws it in UTC for
  // a signed-out reader, so "today" turns over at 00:00 UTC.
  int  today() const;
  // Days in a row with a contribution, ending on the newest day. A newest day
  // with none yet does not break the streak: it counts from the day before.
  int  streak() const;
  // Day number (days since 1970-01-01) of the newest day; only with days() > 0.
  long newestDay() const { return base + last; }
  // Contributions on that day number, capped at 255; 0 for a day not read.
  int  at(long day) const;

 private:
  enum State : uint8_t { SeekDate, ReadDate, SeekTip, SeekTipEnd, ReadCount };
  State   state = SeekDate;
  Needle  dateKey{"data-date=\""};
  Needle  tipKey{"<tool-tip"};
  char    date[10];
  uint8_t dateLen = 0;
  long    base = 0;        // day number of count[0]
  bool    haveBase = false;
  int     idx = -1;        // slot of the day being read
  int     last = -1;       // newest slot seen
  int     seen = 0;
  int     value = 0;
  bool    sawDigit = false;
  uint8_t count[DAYS] = {};   // contributions per day, capped at 255

  void closeDay();
};

// Days since 1970-01-01 for a proleptic Gregorian date.
long daysFromCivil(int y, unsigned m, unsigned d);

}  // namespace ghscan
