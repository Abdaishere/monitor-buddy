#include "odoo_fmt.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void odooHours(float hours, char *out, size_t len) {
  if (len == 0) return;
  const long minutes = hours > 0 ? lroundf(hours * 60.0f) : 0;
  snprintf(out, len, "%ld:%02ld", minutes / 60, minutes % 60);
}

void odooStage(const char *stage, char *out, size_t len) {
  if (len == 0) return;
  snprintf(out, len, "%s", stage ? stage : "");
  size_t n = strlen(out);
  const char *open = strrchr(out, '(');
  if (open && n && out[n - 1] == ')' && open > out) n = (size_t)(open - out);
  while (n && out[n - 1] == ' ') n--;
  out[n] = '\0';
}

void odooAscii(const char *in, char *out, size_t len, size_t maxChars) {
  if (len == 0) return;
  size_t chars = 0, used = 0;
  for (const unsigned char *p = (const unsigned char *)(in ? in : ""); *p; p++) {
    if (*p >= 0x80 && *p < 0xC0) continue;   // continuation byte: its character is already counted
    const char c = *p >= 0x80 ? '?' : *p < 0x20 ? ' ' : (char)*p;
    if (used < len - 1) out[used++] = c;
    chars++;
  }
  out[used] = '\0';
  const size_t limit = maxChars < len - 1 ? maxChars : len - 1;
  if (chars > limit && limit >= 2) memcpy(out + limit - 2, "..", 3);
}

int64_t odooEpoch(const char *utc) {
  int year, month, day, hour, minute, second;
  char end;
  if (!utc || sscanf(utc, "%4d-%2d-%2d %2d:%2d:%2d%c", &year, &month, &day, &hour, &minute, &second, &end) != 6) return -1;
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59
      || hour < 0 || minute < 0 || second < 0) return -1;
  // Days since 1970 by the civil calendar, with the year starting in March.
  year -= month <= 2;
  const int64_t era = year / 400, yearOfEra = year - era * 400;
  const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t days = era * 146097 + yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear - 719468;
  return days * 86400 + hour * 3600 + minute * 60 + second;
}

uint8_t odooWrap(const char *text, char *lines, size_t width, uint8_t maxLines) {
  uint8_t count = 0;
  const char *at = text ? text : "";
  while (count < maxLines) {
    while (*at == ' ') at++;
    if (!*at) break;
    size_t take = strlen(at);
    if (take > width) {
      take = width;
      while (take > 0 && at[take] != ' ') take--;   // back to the last space that fits
      if (take == 0) take = width;                  // one long word: cut it
    }
    char *line = lines + count++ * (width + 1);
    memcpy(line, at, take);
    while (take > 0 && line[take - 1] == ' ') take--;
    line[take] = '\0';
    at += strlen(line);
  }
  return count;
}
