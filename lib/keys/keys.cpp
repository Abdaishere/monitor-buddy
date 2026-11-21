#include "keys.h"
#include <string.h>

bool keysLinkValid(const char *hex) {
  return hex && strlen(hex) == KEYS_LINK_LEN && strspn(hex, "0123456789abcdefABCDEF") == KEYS_LINK_LEN;
}

bool keysUpdateValid(const char *pem) {
  static const char HEAD[] = "-----BEGIN PUBLIC KEY-----\n";
  static const char TAIL[] = "-----END PUBLIC KEY-----";
  if (!pem) return false;
  size_t len = strlen(pem);
  if (len > KEYS_UPDATE_MAX) return false;
  if (len > 0 && pem[len - 1] == '\n') len--;
  const size_t head = sizeof(HEAD) - 1, tail = sizeof(TAIL) - 1;
  if (len < head + tail + 2 || strncmp(pem, HEAD, head) != 0 || strncmp(pem + len - tail, TAIL, tail) != 0) return false;
  if (pem[len - tail - 1] != '\n') return false;
  size_t digits = 0;
  for (size_t i = head; i < len - tail; i++) {
    const char c = pem[i];
    const bool base64 = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
    if (base64) digits++;
    else if (c != '\n') return false;
  }
  return digits >= 64;
}
