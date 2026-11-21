#pragma once
// Text helpers for the Odoo page. Pure C++ with no Arduino headers, so they
// are unit-tested on the PC (pio test -e native).
#include <stddef.h>
#include <stdint.h>

// Hours as "h:mm", rounded to the minute: 2.0 -> "2:00", 7.75 -> "7:45".
// Negative hours give "0:00".
void odooHours(float hours, char *out, size_t len);

// A stage name without its trailing "(...)" part:
// "Development (Team)" -> "Development".
void odooStage(const char *stage, char *out, size_t len);

// UTF-8 text the 6x8 screen font can draw: every non-ASCII character becomes
// one '?', control characters become spaces. Text longer than maxChars (or
// than out holds) is cut and ends in "..".
void odooAscii(const char *in, char *out, size_t len, size_t maxChars);

// A UTC time as Odoo writes it, "YYYY-MM-DD HH:MM:SS", in seconds since 1970.
// -1 for anything else.
int64_t odooEpoch(const char *utc);

// Breaks text at its spaces into lines of at most `width` characters; a longer
// word is cut. `lines` holds maxLines lines of width + 1 bytes each. Returns
// the number of lines used; text past the last line is dropped.
uint8_t odooWrap(const char *text, char *lines, size_t width, uint8_t maxLines);
