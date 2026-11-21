#pragma once
#include <Arduino.h>
class WiFiClient {
 public:
  void setTimeout(uint32_t) {}
  void stop() {}
};
class WiFiClientSecure : public WiFiClient {
 public:
  void useBuiltinCACertBundle() { verified = true; }
  void setCACertBundle(const uint8_t *, size_t = 0) { verified = true; }
  void setInsecure() { verified = false; }
  void setHandshakeTimeout(unsigned long) {}
  bool verified = false;
};
