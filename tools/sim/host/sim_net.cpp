// Monitor Buddy simulator: the radio. Wi-Fi station, the stand-in for
// AyresWiFiManager, the link's UDP socket, HTTP to the mock internet, SNTP.
#include "sim.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <AyresWiFiManager.h>
#include <esp_sntp.h>
#include <ArduinoJson.h>
#include <arpa/inet.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// In a browser there is no socket: the page's Module.mbsimHttp(request) plays
// the mock internet and returns the whole answer as bytes, or null for nobody there.
EM_JS(char *, mbsimHttpJs, (const char *request, int *length), {
  const answer = Module.mbsimHttp ? Module.mbsimHttp(UTF8ToString(request)) : null;
  if (!answer) { HEAP32[length >> 2] = 0; return 0; }
  const at = _malloc(answer.length || 1);
  HEAPU8.set(answer, at);
  HEAP32[length >> 2] = answer.length;
  return at;
});
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#endif

using namespace sim;

WiFiClass WiFi;

// ── Station ─────────────────────────────────────────────────
static std::string staSsid, staPassword, joinSsid, joinPassword;
static bool staUp = false, joining = false;
static uint64_t joinDoneAt = 0;
static wl_status_t staStatus = WL_DISCONNECTED;
static const uint64_t JOIN_US = 1800000, WRONG_PASSWORD_US = 3200000;

static const Ap *findAp(const std::string &ssid) {
  for (const Ap &ap : board.aps) if (ap.ssid == ssid) return &ap;
  return nullptr;
}

namespace sim {
void wifiPoll() {
  if (joining && nowUs() >= joinDoneAt) {
    const Ap *ap = findAp(joinSsid);
    if (!ap) {
      staStatus = WL_NO_SSID_AVAIL;   // and the attempt goes on: the network may come into range
      joinDoneAt = nowUs() + JOIN_US;
    } else if (ap->password != joinPassword) {
      joining = false;
      staStatus = WL_CONNECT_FAILED;
    } else {
      joining = false;
      staUp = true;
      staSsid = joinSsid;
      staPassword = joinPassword;
      staStatus = WL_CONNECTED;
    }
  }
  if (staUp) {
    const Ap *ap = findAp(staSsid);
    if (!ap || ap->password != staPassword) { staUp = false; staStatus = WL_CONNECTION_LOST; }
  }
}
bool wifiConnected() { wifiPoll(); return staUp; }
int wifiRssi() { const Ap *ap = staUp ? findAp(staSsid) : nullptr; return ap ? ap->rssi : 0; }
std::string wifiSsid() { return staUp ? staSsid : std::string(); }
void wifiJoin(const std::string &ssid, const std::string &password) {
  staUp = false;
  joining = true;
  joinSsid = ssid;
  joinPassword = password;
  const Ap *ap = findAp(ssid);
  joinDoneAt = nowUs() + (ap && ap->password != password ? WRONG_PASSWORD_US : JOIN_US);
  staStatus = WL_DISCONNECTED;
}
void wifiLeave() { staUp = false; joining = false; staStatus = WL_DISCONNECTED; }
bool wifiJoining() { return joining; }
}  // namespace sim

wl_status_t WiFiClass::status() { wifiPoll(); return staStatus; }
IPAddress WiFiClass::localIP() { return wifiConnected() ? IPAddress(192, 168, 1, 50) : IPAddress(0, 0, 0, 0); }
int8_t WiFiClass::RSSI() { return (int8_t)wifiRssi(); }
String WiFiClass::SSID() { return String(wifiSsid()); }
String WiFiClass::SSID(uint8_t index) { return index < board.aps.size() ? String(board.aps[index].ssid) : String(); }
int16_t WiFiClass::scanNetworks() { spendUs(2400000); return (int16_t)board.aps.size(); }
uint8_t WiFiClass::softAPgetStationNum() { return (uint8_t)board.apClients; }
String WiFiClass::macAddress() {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", board.mac[0], board.mac[1], board.mac[2], board.mac[3], board.mac[4], board.mac[5]);
  return String(b);
}
int WiFiClass::hostByName(const char *host, IPAddress &result) {
  if (!wifiConnected() || !board.internet) { spendUs(1500000); return 0; }
  spendUs(40000);
  uint32_t hash = 2166136261u;
  for (const char *p = host; *p; p++) hash = (hash ^ (uint8_t)*p) * 16777619u;
  result = IPAddress(162, 159, (uint8_t)(hash >> 8), (uint8_t)(hash | 1));
  return 1;
}

// ── AyresWiFiManager ────────────────────────────────────────
AyresWiFiManager *AyresWiFiManager::instance = nullptr;

void AyresWiFiManager::loadCredentials() {
  ssid.clear();
  password.clear();
  if (!LittleFS.exists("/wifi.json")) return;
  File f = LittleFS.open("/wifi.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok && doc["ssid"].is<const char *>()) {
    ssid = doc["ssid"].as<const char *>();
    password = doc["password"] | "";
  }
  f.close();
}
void AyresWiFiManager::begin() {
  instance = this;
  LittleFS.begin(true);
  loadCredentials();
}
void AyresWiFiManager::run() {
  if (ssid.empty()) {
    if (policy == FallbackPolicy::ON_FAIL || policy == FallbackPolicy::NO_CREDENTIALS_ONLY) openPortal();
    return;
  }
  delay(2000);   // the library's button window
  wifiJoin(ssid, password);
  const uint32_t started = millis();
  while (!wifiConnected() && wifiJoining() && millis() - started < 15000UL) delay(100);
  if (wifiConnected()) return;
  wifiLeave();
  failedAt = millis() | 1;
  if (policy == FallbackPolicy::ON_FAIL) openPortal();
}
bool AyresWiFiManager::isConnected() { return !portal && wifiConnected(); }
int AyresWiFiManager::getSignalStrength() { return wifiRssi(); }
void AyresWiFiManager::openPortal() {
  if (portal) return;
  portal = true;
  attempting = false;
  wifiLeave();
  portalSeenAt = millis();
}
void AyresWiFiManager::closePortal() { portal = false; }
void AyresWiFiManager::update() {
  if (!portal) return;
  if (apClientCheck && board.apClients > 0) portalSeenAt = millis();
  if (portalTimeoutS && millis() - portalSeenAt > portalTimeoutS * 1000UL) {
    Serial.println("AWM: portal timeout");
    closePortal();
    if (!ssid.empty()) forzarReconexion();
  }
}
void AyresWiFiManager::forzarReconexion() {
  if (ssid.empty()) return;
  wifiJoin(ssid, password);
  attempting = true;
  attemptAt = millis();
}
void AyresWiFiManager::reintentarConexionSiNecesario() {
  if (portal || ssid.empty() || !autoReconnect) return;
  if (wifiConnected()) { attempting = false; failedAt = 0; return; }
  const uint32_t now = millis();
  if (attempting) {
    if (!wifiJoining() || now - attemptAt >= attemptMs) {   // refused, or the window is over
      attempting = false;
      wifiLeave();
      failedAt = now | 1;
    }
  } else if (!failedAt || now - failedAt >= backoffMs) {
    forzarReconexion();
  }
}
// What the portal's "save" does: /wifi.json, then a restart.
void AyresWiFiManager::simPortalSave(const char *newSsid, const char *newPassword) {
  if (!portal) return;
  JsonDocument doc;
  doc["ssid"] = newSsid;
  doc["password"] = newPassword;
  File f = LittleFS.open("/wifi.json", "w");
  if (f) { serializeJson(doc, f); f.close(); }
  sim::requestRestart(3);
}
// The portal's "erase": the saved network and the brightness go, then a restart.
void AyresWiFiManager::simPortalErase() {
  if (!portal) return;
  LittleFS.remove("/wifi.json");
  LittleFS.remove("/display.json");
  sim::requestRestart(3);
}

// ── UDP ─────────────────────────────────────────────────────
uint8_t WiFiUDP::begin(uint16_t port) { bound = udpBind(port); return bound ? 1 : 0; }
int WiFiUDP::parsePacket() {
  if (!bound || !wifiConnected()) return 0;
  at = 0;
  if (!udpReceive(rx)) { rx.data.clear(); return 0; }
  return (int)rx.data.size();
}
int WiFiUDP::read(uint8_t *buffer, size_t size) {
  const size_t n = std::min(size, rx.data.size() - at);
  memcpy(buffer, rx.data.data() + at, n);
  at += n;
  return (int)n;
}
int WiFiUDP::beginPacket(IPAddress ip, uint16_t port) {
  tx.data.clear();
  tx.ip = (uint32_t)ip;
  tx.port = port;
  tx.real = rx.real;
  return wifiConnected() ? 1 : 0;
}
size_t WiFiUDP::write(const uint8_t *data, size_t size) { tx.data.insert(tx.data.end(), data, data + size); return size; }
int WiFiUDP::endPacket() { udpSend(tx); return 1; }

// ── HTTP ────────────────────────────────────────────────────
bool HTTPClient::begin(WiFiClient &, const String &u) {
  url = u.v;
  body.clear();
  headers.clear();
  cutBody = false;
  return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}

int HTTPClient::GET() {
  board.httpRequests++;
  board.httpLog.push_back(url);
  if (board.httpLog.size() > 200) board.httpLog.erase(board.httpLog.begin());
  const bool https = url.compare(0, 8, "https://") == 0;
  const std::string rest = url.substr(https ? 8 : 7);
  const size_t slash = rest.find('/');
  const std::string host = rest.substr(0, slash), path = slash == std::string::npos ? "/" : rest.substr(slash);
  if (!wifiConnected() || !board.internet || board.httpMock.empty()) {
    spendUs((uint64_t)connectTimeoutMs * 1000ULL);   // nobody answers: the connect times out
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  const std::string request = "GET /" + host + path + " HTTP/1.0\r\nHost: " + host + "\r\nUser-Agent: " + userAgent
                              + "\r\nX-Sim-Scheme: " + (https ? "https" : "http") + "\r\n" + headers + "Connection: close\r\n\r\n";
  std::string response;
#ifdef __EMSCRIPTEN__
  int length = 0;
  char *answer = mbsimHttpJs(request.c_str(), &length);
  if (!answer) {
    spendUs((uint64_t)connectTimeoutMs * 1000ULL);
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  response.assign(answer, (size_t)length);
  free(answer);
#else
  const size_t colon = board.httpMock.find(':');
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)atoi(board.httpMock.substr(colon + 1).c_str()));
  inet_pton(AF_INET, board.httpMock.substr(0, colon).c_str(), &addr.sin_addr);
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    if (fd >= 0) close(fd);
    spendUs((uint64_t)connectTimeoutMs * 1000ULL);
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }
  size_t sent = 0;
  while (sent < request.size()) {
    const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
    if (n <= 0) break;
    sent += (size_t)n;
  }
  char buf[8192];
  for (;;) {
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) break;
    response.append(buf, (size_t)n);
  }
  close(fd);
#endif
  const size_t headEnd = response.find("\r\n\r\n");
  if (response.compare(0, 5, "HTTP/") != 0 || headEnd == std::string::npos) {
    spendUs((uint64_t)timeoutMs * 1000ULL);
    return HTTPC_ERROR_READ_TIMEOUT;
  }
  const int status = atoi(response.c_str() + response.find(' ') + 1);
  std::string head = response.substr(0, headEnd);
  for (auto &c : head) c = (char)tolower((unsigned char)c);
  body = response.substr(headEnd + 4);
  // What the mock says about the line: its delay, a read that never answers, a body cut short.
  uint64_t latencyMs = https ? 420 : 120;
  size_t at = head.find("x-sim-latency-ms:");
  if (at != std::string::npos) latencyMs = strtoull(head.c_str() + at + 17, nullptr, 10);
  spendUs(latencyMs * 1000ULL);
  if (head.find("x-sim-timeout:") != std::string::npos) {
    spendUs((uint64_t)timeoutMs * 1000ULL);
    body.clear();
    return HTTPC_ERROR_READ_TIMEOUT;
  }
  cutBody = head.find("x-sim-cut:") != std::string::npos;
  return status;
}

int HTTPClient::writeToStream(Stream *stream) {
  const size_t n = cutBody ? body.size() / 2 : body.size();
  for (size_t at = 0; at < n; at += 512) stream->write((const uint8_t *)body.data() + at, std::min((size_t)512, n - at));
  return cutBody ? HTTPC_ERROR_READ_TIMEOUT : (int)n;
}

// ── SNTP and the time zone ──────────────────────────────────
static bool sntpRunning = false, sntpCompleted = false;
static uint64_t sntpDueAt = 0;

namespace sim {
void sntpPoll() {
  if (!sntpRunning || nowUs() < sntpDueAt) return;
  if (!staUp || !board.internet) { sntpDueAt = nowUs() + 15000000; return; }   // no answer: it asks again
  rtcSet(trueNowUs());
  sntpCompleted = true;
  sntpDueAt = nowUs() + 3600ULL * 1000000ULL;   // SNTP's own hourly resync
}
}  // namespace sim

sntp_sync_status_t sntp_get_sync_status() {
  sim::sntpPoll();
  if (sntpCompleted) { sntpCompleted = false; return SNTP_SYNC_STATUS_COMPLETED; }   // read and clear
  return SNTP_SYNC_STATUS_RESET;
}
void configTzTime(const char *tz, const char *server1, const char *, const char *) {
  setenv("TZ", tz, 1);
  tzset();
  sntpRunning = server1 && server1[0];
  sntpCompleted = false;
  sntpDueAt = nowUs() + 900000;
}
void configTime(long gmtOffset_sec, int daylightOffset_sec, const char *server1, const char *server2, const char *server3) {
  char tz[32];
  const long offset = -(gmtOffset_sec + daylightOffset_sec);
  snprintf(tz, sizeof(tz), "UTC%+ld:%02ld", offset / 3600, labs(offset % 3600) / 60);
  configTzTime(tz, server1, server2, server3);
}
bool getLocalTime(struct tm *info, uint32_t ms) {
  const uint32_t started = millis();
  for (;;) {
    time_t now = time(nullptr);
    localtime_r(&now, info);
    if (info->tm_year > (2016 - 1900)) return true;
    if (millis() - started >= ms) return false;
    delay(10);
  }
}
