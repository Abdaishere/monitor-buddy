#pragma once
// A push button read by polling. Pure C++ with no Arduino headers, so it is
// unit-tested on the PC (pio test -e native).
#include <stdint.h>

// press() is true once for each press. A change within debounceMs of the last
// one taken is ignored, and a button that is down at the first read counts
// only after a release.
struct ButtonEdge {
  uint32_t debounceMs;
  bool down = true;          // the state taken last
  uint32_t changedAt = 0;    // when, in the caller's milliseconds
  bool press(bool isDown, uint32_t now);
};
