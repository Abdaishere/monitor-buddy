// The radio for the host build. Networks in range are scripted (sim::board.aps).
#pragma once
#include <Arduino.h>
typedef enum { WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_SCAN_COMPLETED = 2, WL_CONNECTED = 3,
               WL_CONNECT_FAILED = 4, WL_CONNECTION_LOST = 5, WL_DISCONNECTED = 6 } wl_status_t;
typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
#define WIFI_MODE_STA WIFI_STA
class WiFiClass {
 public:
  bool mode(wifi_mode_t m) { currentMode = m; return true; }
  wifi_mode_t getMode() { return currentMode; }
  wl_status_t status();
  IPAddress localIP();
  int8_t RSSI();
  String SSID();
  String SSID(uint8_t index);
  int16_t scanNetworks();
  void scanDelete() {}
  int hostByName(const char *host, IPAddress &result);
  uint8_t softAPgetStationNum();
  String macAddress();
 private:
  wifi_mode_t currentMode = WIFI_OFF;
};
extern WiFiClass WiFi;
