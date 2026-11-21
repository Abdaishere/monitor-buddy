#pragma once
// The Buddy's two keys: shape checks only. Pure C++ with no Arduino headers,
// so it is unit-tested on the PC (pio test -e native).
//
// The keys arrive over USB with "@MB KEYS" and are kept in their own file,
// apart from the settings. Nothing ever sends them back: INFO only reports a
// short fingerprint of each.
#include <stddef.h>

// The link key as text: 64 hex digits (AES-256).
static const size_t KEYS_LINK_LEN = 64;
// Room for a P-256 public key in PEM (178 characters) with some to spare.
static const size_t KEYS_UPDATE_MAX = 255;

// Exactly 64 hex digits.
bool keysLinkValid(const char *hex);

// A PEM public key: "-----BEGIN PUBLIC KEY-----", base64 lines, then
// "-----END PUBLIC KEY-----" and an optional last newline; at most
// KEYS_UPDATE_MAX characters. Whether it is a usable key shows when it is parsed.
bool keysUpdateValid(const char *pem);
