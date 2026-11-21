#include "button.h"

bool ButtonEdge::press(bool isDown, uint32_t now) {
  if (isDown == down || now - changedAt < debounceMs) return false;
  down = isDown;
  changedAt = now;
  return isDown;
}
