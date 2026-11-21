// Monitor Buddy simulator: the fake board under the real firmware.
//
// One process, one OS thread. The firmware's tasks (loop, net, knock) run as
// coroutines on a virtual clock: time moves only when a task waits, so a run
// is repeatable to the pixel and a simulated day takes minutes.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <deque>
#include <map>

namespace sim {

// ── Virtual clock and tasks ─────────────────────────────────
uint64_t nowUs();                       // microseconds since this boot
void     sleepUs(uint64_t us);          // the calling task waits
void     spendUs(uint64_t us);          // work that takes time (SPI, flash, radio)
void     spin();                        // called by millis()/micros(): breaks busy waits
int      taskCreate(void (*fn)(void *), void *arg, const char *name, int priority);
const char *taskName();

struct Mutex;
Mutex *mutexNew();
bool   mutexTake(Mutex *m, uint64_t timeoutUs);
void   mutexGive(Mutex *m);

[[noreturn]] void restart(int reason, const char *why);   // ESP.restart(), a crash, a brown-out
void requestRestart(int reason);                           // the same, asked from outside the firmware

// ── Wall clock ──────────────────────────────────────────────
// trueEpochUs: the time out in the world. rtcEpochUs: what the chip's clock
// says, wrong until SNTP has set it.
int64_t trueNowUs();
int64_t rtcNowUs();
void    rtcSet(int64_t epochUs);

// ── The board's state, as the tests script it ───────────────
struct Ap { std::string ssid, password; int rssi; };

struct Board {
  // display
  uint16_t panel[320 * 172];          // what the LCD shows, as the user sees it
  bool     panelOn = false;           // the init sequence sent DISPON
  bool     panelInit = false;
  uint8_t  rotation = 0;
  int      backlight = 0;             // PWM duty, 0-255
  uint64_t flushes = 0, flushedPixels = 0;
  uint32_t spiHz = 40000000;
  // touch (display coordinates) and BOOT
  bool     touchPresent = true, touchDown = false;
  int      touchX = 0, touchY = 0;
  bool     bootPressed = false;
  // motion sensor: the pull in g and the turn in degrees a second
  bool     imuPresent = true;
  float    accel[3] = {0.0f, -1.0f, 0.0f};
  float    gyro[3]  = {0, 0, 0};
  // radio
  std::vector<Ap> aps;                // networks in range
  bool     internet = true;           // the router reaches the internet
  int      apClients = 0;             // phones joined to the setup hotspot
  std::string httpMock;               // host:port of the mock internet, "" = none
  // card: "none", "ok" or "nofat"
  std::string card = "ok";
  // identity
  int      resetReason = 1;
  bool     crashDump = false;
  uint8_t  mac[6] = {0x58, 0x8c, 0x81, 0x00, 0x51, 0x4d};
  std::string dataDir;                // littlefs/, sd/, ota.json, slot-*.bin
  // counters the tests read
  uint64_t httpRequests = 0;
  std::vector<std::string> httpLog;
};
extern Board board;

// ── Serial and UDP, both ways ───────────────────────────────
void serialOut(const uint8_t *data, size_t len);      // firmware -> world
int  serialIn();                                      // world -> firmware, -1 = empty
int  serialInAvailable();
struct Datagram { std::vector<uint8_t> data; uint32_t ip; uint16_t port; bool real; };
bool udpBind(uint16_t port);
bool udpReceive(Datagram &out);
void udpSend(const Datagram &d);

// ── Wi-Fi station state (WiFi.h and the fake AyresWiFiManager share it) ──
bool wifiConnected();
int  wifiRssi();
std::string wifiSsid();
void wifiJoin(const std::string &ssid, const std::string &password);   // starts an attempt
void wifiLeave();
bool wifiJoining();
void wifiPoll();                         // lets attempts finish and links drop

// ── Files ───────────────────────────────────────────────────
std::string fsRoot(const char *which);   // "littlefs" or "sd"

void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}  // namespace sim
