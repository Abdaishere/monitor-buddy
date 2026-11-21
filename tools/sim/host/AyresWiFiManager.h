// A stand-in for AyresWiFiManager 2.5.0 with the calls the firmware makes.
// It keeps the library's behaviour as the firmware relies on it: credentials
// in /wifi.json, a blocking first connect in run(), the setup hotspot only
// with no credentials (NO_CREDENTIALS_ONLY), a timed reconnect, and a portal
// that closes after its timeout. The portal's web pages are not served; a
// test "saves" or "erases" through the simulator's control channel.
#pragma once
#include <Arduino.h>
class AyresWiFiManager {
 public:
  enum class FallbackPolicy { ON_FAIL, NO_CREDENTIALS_ONLY, SMART_RETRIES, BUTTON_ONLY, NEVER };
  AyresWiFiManager(int ledPin = 2, int buttonPin = 0) : ledPin(ledPin), buttonPin(buttonPin) {}
  void setHostname(const char *name) { hostname = name; }
  void setAPCredentials(const char *ssid, const char *pass) { apSsid = ssid; apPass = pass; }
  void setCaptivePortal(bool) {}
  void setPortalTimeout(uint32_t seconds) { portalTimeoutS = seconds; }
  void setAPClientCheck(bool on) { apClientCheck = on; }
  void setWebClientCheck(bool) {}
  void setFallbackPolicy(FallbackPolicy p) { policy = p; }
  void setAutoReconnect(bool on) { autoReconnect = on; }
  void setLedAuto(bool) {}
  void setReconnectAttemptMs(uint32_t ms) { attemptMs = ms; }
  void setReconnectBackoffMs(uint32_t ms) { backoffMs = ms; }
  void setTimeSync(bool) {}
  void begin();
  void run();
  void update();
  bool isConnected();
  bool isPortalActive() { return portal; }
  bool tieneCredenciales() { return !ssid.empty(); }
  int getSignalStrength();
  void openPortal();
  void closePortal();
  void reintentarConexionSiNecesario();
  void forzarReconexion();
  // the simulator's hooks: what a phone does on the portal's pages
  void simPortalSave(const char *ssid, const char *password);
  void simPortalErase();
  void simPortalRequest() { portalSeenAt = millis(); }
  static AyresWiFiManager *instance;
 private:
  void loadCredentials();
  int ledPin, buttonPin;
  std::string hostname, apSsid, apPass, ssid, password;
  FallbackPolicy policy = FallbackPolicy::ON_FAIL;
  bool portal = false, autoReconnect = true, apClientCheck = false;
  uint32_t portalTimeoutS = 300, portalSeenAt = 0, attemptMs = 8000, backoffMs = 5000;
  uint32_t attemptAt = 0, failedAt = 0;
  bool attempting = false;
};
