#include "github_scan.h"

namespace ghscan {

bool Needle::step(char c) {
  if (c == text[at]) {
    at++;
    if (text[at] == '\0') { at = 0; return true; }
    return false;
  }
  // Restart on a mismatch, with no backtracking. That can miss a word only
  // when its own beginning sits right in front of it ("data-data-date="),
  // which GitHub's markup and JSON never produce: an attribute follows a
  // space, a key follows a quote.
  at = (c == text[0]) ? 1 : 0;
  return false;
}

void StarScan::feed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = (char)data[i];
    if (inNumber) {
      if (c >= '0' && c <= '9') { value = value * 10 + (c - '0'); sawDigit = true; continue; }
      if (c == ' ' && !sawDigit) continue;
      if (sawDigit) { stars += value; repos++; }
      inNumber = false;
    }
    if (key.step(c)) { inNumber = true; sawDigit = false; value = 0; }
  }
}

long daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + (long)doe - 719468L;
}

static int digits(const char *s, int n) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (s[i] - '0');
  }
  return v;
}

void ContribScan::closeDay() {
  if (idx >= 0) {
    count[idx] = (uint8_t)(value > 255 ? 255 : value);
    if (idx > last) last = idx;
    seen++;
  }
  idx = -1;
  state = SeekDate;
}

void ContribScan::feed(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = (char)data[i];
    switch (state) {
      case SeekDate:
        if (dateKey.step(c)) { state = ReadDate; dateLen = 0; }
        break;
      case ReadDate:
        date[dateLen++] = c;
        if (dateLen < 10) break;
        {
          const int y = digits(date, 4), m = digits(date + 5, 2), d = digits(date + 8, 2);
          idx = -1;
          if (y > 0 && m >= 1 && m <= 12 && d >= 1 && d <= 31 && date[4] == '-' && date[7] == '-') {
            const long day = daysFromCivil(y, (unsigned)m, (unsigned)d);
            // The first cell is the oldest Sunday; the slack absorbs a page that
            // ever starts a few days later than its oldest day.
            if (!haveBase) { base = day - 16; haveBase = true; }
            const long slot = day - base;
            if (slot >= 0 && slot < DAYS) idx = (int)slot;
          }
          if (idx < 0) { state = SeekDate; break; }
          state = SeekTip;
          tipKey.reset();
        }
        break;
      case SeekTip:
        if (tipKey.step(c)) state = SeekTipEnd;
        break;
      case SeekTipEnd:
        if (c == '>') { state = ReadCount; value = 0; sawDigit = false; }
        break;
      case ReadCount:
        if (c >= '0' && c <= '9') { if (value < 100000) value = value * 10 + (c - '0'); sawDigit = true; break; }
        if (c == ',' && sawDigit) break;          // "1,024 contributions"
        if ((c == ' ' || c == '\n') && !sawDigit) break;
        closeDay();                               // "No ..." leaves value 0
        break;
    }
  }
}

int ContribScan::today() const { return last < 0 ? 0 : count[last]; }

int ContribScan::at(long day) const {
  const long slot = day - base;
  return haveBase && slot >= 0 && slot <= last ? count[slot] : 0;
}

int ContribScan::streak() const {
  if (last < 0) return 0;
  int i = count[last] ? last : last - 1;
  int n = 0;
  for (; i >= 0 && count[i]; i--) n++;
  return n;
}

}  // namespace ghscan
