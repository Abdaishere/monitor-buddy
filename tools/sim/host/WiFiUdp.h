#pragma once
#include <Arduino.h>
#include <WiFi.h>
class WiFiUDP {
 public:
  uint8_t begin(uint16_t port);
  void stop() {}
  int parsePacket();
  int read(uint8_t *buffer, size_t size);
  int read(char *buffer, size_t size) { return read((uint8_t *)buffer, size); }
  void clear() { rx.data.clear(); at = 0; }
  void flush() { clear(); }
  IPAddress remoteIP() { return IPAddress(rx.ip); }
  uint16_t remotePort() { return rx.port; }
  int beginPacket(IPAddress ip, uint16_t port);
  size_t write(const uint8_t *data, size_t size);
  int endPacket();
 private:
  sim::Datagram rx, tx;
  size_t at = 0;
  bool bound = false;
};
