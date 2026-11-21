// Monitor Buddy simulator: the scheduler, the clocks and the control channel.
//
// The firmware's tasks are coroutines. Exactly one runs at a time, and it runs
// until it waits (delay, vTaskDelay, a mutex, SPI or flash work that takes
// time). The scheduler then moves the virtual clock to the next task due. No
// wall-clock time is involved unless --pace asks for it, so a run is the same
// every time.
#include "sim.h"
#include <Arduino.h>
#include <AyresWiFiManager.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#ifdef __EMSCRIPTEN__
// In a browser (make wasm): the tasks are Emscripten fibers, there is no
// pseudo-terminal and no socket, and the commands come through mbsim_cmd().
#include <emscripten.h>
#include <emscripten/fiber.h>
#define __THROW
typedef emscripten_fiber_t Ctx;
static inline void ctxSwap(Ctx *from, Ctx *to) { emscripten_fiber_swap(from, to); }
#else
#include <ucontext.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pty.h>
#include <termios.h>
typedef ucontext_t Ctx;
static inline void ctxSwap(Ctx *from, Ctx *to) { swapcontext(from, to); }
#endif

void setup();
void loop();

namespace sim {

Board board;

// ── Tasks ───────────────────────────────────────────────────
enum TaskState { READY, BLOCKED, DEAD };
struct Task {
  Ctx ctx;
  void (*fn)(void *);
  void *arg;
  std::string name;
  int priority;
  TaskState state = READY;
  uint64_t wake = 0, seq = 0;
  uint32_t spins = 0;
  char *stack = nullptr;
  char *unwind = nullptr;   // Emscripten: where a fiber's frames go while it waits
};
struct Mutex { Task *owner = nullptr; bool ownedByMain = false; std::deque<Task *> waiters; };

static std::vector<Task *> tasks;
static Task *current = nullptr;
static Ctx mainCtx;
static uint64_t vnow = 0, seqCounter = 0;
static bool stopRequested = false;
static bool restartPending = false;
static int restartReason = 0;
static std::string restartWhy;
static const size_t STACK_BYTES = 1024 * 1024;

// pacing against the real clock (--pace 1 for a live Buddy, 0 = as fast as it goes)
static double pace = 0;
static uint64_t paceVirtualStart = 0;
static struct timespec paceRealStart;

static void paceTo(uint64_t t) {
#ifdef __EMSCRIPTEN__
  return;   // the page paces it: it asks for 40 ms of the Buddy's time per frame
#endif
  if (pace <= 0) return;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  const double elapsed = (now.tv_sec - paceRealStart.tv_sec) + (now.tv_nsec - paceRealStart.tv_nsec) / 1e9;
  const double due = (double)(t - paceVirtualStart) / 1e6 / pace;
  if (due > elapsed) usleep((useconds_t)((due - elapsed) * 1e6));
  else if (elapsed - due > 0.5) {   // fell behind (a breakpoint, a slow host): do not sprint to catch up
    clock_gettime(CLOCK_MONOTONIC, &paceRealStart);
    paceVirtualStart = t;
  }
}

uint64_t nowUs() { return vnow; }

static void toScheduler() {
  Task *self = current;
  self->spins = 0;
  ctxSwap(&self->ctx, &mainCtx);
}

void sleepUs(uint64_t us) {
  if (!current) { vnow += us; return; }
  current->wake = vnow + us;
  current->seq = ++seqCounter;
  current->state = READY;
  toScheduler();
}

void spendUs(uint64_t us) { if (us) sleepUs(us); }

// A loop that only watches millis() would never end on a clock that stands
// still while code runs: after enough looks at the clock, a millisecond passes.
void spin() {
  if (current && ++current->spins > 20000) sleepUs(1000);
}

#ifdef __EMSCRIPTEN__
static const size_t UNWIND_BYTES = 256 * 1024;
static char mainUnwind[256 * 1024];
static void taskEntry(void *arg) {
  Task *t = (Task *)arg;
  t->fn(t->arg);
  t->state = DEAD;
  for (;;) ctxSwap(&t->ctx, &mainCtx);
}
#else
static void taskEntry(unsigned lo, unsigned hi) {
  Task *t = (Task *)(((uintptr_t)hi << 32) | (uintptr_t)lo);
  t->fn(t->arg);
  t->state = DEAD;
  swapcontext(&t->ctx, &mainCtx);
}
#endif

int taskCreate(void (*fn)(void *), void *arg, const char *name, int priority) {
  Task *t = new Task();
  t->fn = fn;
  t->arg = arg;
  t->name = name ? name : "task";
  t->priority = priority;
  t->stack = (char *)malloc(STACK_BYTES);
  if (!t->stack) { delete t; return 0; }
#ifdef __EMSCRIPTEN__
  t->unwind = (char *)malloc(UNWIND_BYTES);
  if (!t->unwind) { free(t->stack); delete t; return 0; }
  emscripten_fiber_init(&t->ctx, taskEntry, t, t->stack, STACK_BYTES, t->unwind, UNWIND_BYTES);
#else
  getcontext(&t->ctx);
  t->ctx.uc_stack.ss_sp = t->stack;
  t->ctx.uc_stack.ss_size = STACK_BYTES;
  t->ctx.uc_link = &mainCtx;
  const uintptr_t p = (uintptr_t)t;
  makecontext(&t->ctx, (void (*)())taskEntry, 2, (unsigned)(p & 0xFFFFFFFFu), (unsigned)(p >> 32));
#endif
  t->wake = vnow;
  t->seq = ++seqCounter;
  tasks.push_back(t);
  return 1;
}

const char *taskName() { return current ? current->name.c_str() : "main"; }

Mutex *mutexNew() { return new Mutex(); }

bool mutexTake(Mutex *m, uint64_t timeoutUs) {
  if (!m) return false;
  if (!current) {
    if (m->owner || m->ownedByMain) return false;
    m->ownedByMain = true;
    return true;
  }
  if (!m->owner && !m->ownedByMain) { m->owner = current; return true; }
  if (timeoutUs == 0) return false;
  current->state = BLOCKED;
  current->wake = timeoutUs == UINT64_MAX ? UINT64_MAX : vnow + timeoutUs;
  current->seq = ++seqCounter;
  m->waiters.push_back(current);
  toScheduler();
  if (m->owner == current) return true;
  for (auto it = m->waiters.begin(); it != m->waiters.end(); ++it) {
    if (*it == current) { m->waiters.erase(it); break; }
  }
  return false;
}

void mutexGive(Mutex *m) {
  if (!m) return;
  m->ownedByMain = false;
  m->owner = nullptr;
  if (!m->waiters.empty()) {
    Task *next = m->waiters.front();
    m->waiters.pop_front();
    m->owner = next;
    next->state = READY;
    next->wake = vnow;
    next->seq = ++seqCounter;
  }
}

void requestRestart(int reason) { restartPending = true; restartReason = reason; }

void restart(int reason, const char *why) {
  restartPending = true;
  restartReason = reason;
  restartWhy = why ? why : "";
  if (current) {
    current->state = DEAD;
    for (;;) ctxSwap(&current->ctx, &mainCtx);
  }
  for (;;) pause();
}

// Runs the tasks until the virtual clock reaches `limit`, a stop is asked for,
// or the firmware restarts.
static void runUntil(uint64_t limit) {
  stopRequested = false;
  while (!restartPending && !stopRequested) {
    Task *next = nullptr;
    for (Task *t : tasks) {
      if (t->state == DEAD) continue;
      if (t->state == BLOCKED && t->wake == UINT64_MAX) continue;
      if (!next || t->wake < next->wake
          || (t->wake == next->wake && (t->priority > next->priority
                                        || (t->priority == next->priority && t->seq < next->seq)))) next = t;
    }
    if (!next || next->wake > limit) { paceTo(limit); vnow = limit; return; }
    if (next->wake > vnow) { paceTo(next->wake); vnow = next->wake; }
    if (next->state == BLOCKED) next->state = READY;   // its wait timed out
    current = next;
    ctxSwap(&mainCtx, &next->ctx);
    current = nullptr;
  }
}

// ── Wall clock ──────────────────────────────────────────────
static int64_t trueEpochAtBootUs = 0, rtcEpochAtBootUs = 0;
int64_t trueNowUs() { return trueEpochAtBootUs + (int64_t)vnow; }
void sntpPoll();
int64_t rtcNowUs() { sntpPoll(); return rtcEpochAtBootUs + (int64_t)vnow; }
void rtcSet(int64_t epochUs) { rtcEpochAtBootUs = epochUs - (int64_t)vnow; }

// ── Serial ──────────────────────────────────────────────────
static std::string serialTx;            // drained by the control channel
static std::deque<uint8_t> serialRx;
static FILE *serialLog = nullptr;
static bool serialEcho = false;
static int ptyFd = -1;
static std::string stopOnSerial;        // run stops when the output contains this
static size_t serialScanFrom = 0;

void serialOut(const uint8_t *data, size_t len) {
  serialTx.append((const char *)data, len);
  if (serialTx.size() > 4 * 1024 * 1024) { serialTx.erase(0, 2 * 1024 * 1024); serialScanFrom = 0; }
  if (serialLog) { fwrite(data, 1, len, serialLog); fflush(serialLog); }
  if (serialEcho) { fwrite(data, 1, len, stderr); fflush(stderr); }
#ifndef __EMSCRIPTEN__
  if (ptyFd >= 0) { ssize_t n = ::write(ptyFd, data, len); (void)n; }
#endif
  if (!stopOnSerial.empty()) {
    const size_t from = serialScanFrom > stopOnSerial.size() ? serialScanFrom - stopOnSerial.size() : 0;
    if (serialTx.find(stopOnSerial, from) != std::string::npos) stopRequested = true;
    serialScanFrom = serialTx.size();
  }
}

static void ptyPump() {
#ifdef __EMSCRIPTEN__
  return;
#endif
  if (ptyFd < 0) return;
  uint8_t buf[512];
  for (;;) {
    const ssize_t n = ::read(ptyFd, buf, sizeof(buf));
    if (n <= 0) break;
    for (ssize_t i = 0; i < n; i++) serialRx.push_back(buf[i]);
  }
}
int serialInAvailable() { ptyPump(); return (int)serialRx.size(); }
int serialIn() {
  ptyPump();
  if (serialRx.empty()) return -1;
  const uint8_t c = serialRx.front();
  serialRx.pop_front();
  return c;
}

// ── UDP ─────────────────────────────────────────────────────
static std::deque<Datagram> udpRx;
static std::vector<std::vector<uint8_t>> udpTx;
static int udpFd = -1;
static uint16_t udpRealPort = 0;        // 0 = no real socket, packets come over the control channel
static bool stopOnUdp = false;

bool udpBind(uint16_t port) {
#ifdef __EMSCRIPTEN__
  return true;
#else
  if (!udpRealPort || udpFd >= 0) return true;
  udpFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (udpFd < 0) return false;
  int one = 1;
  setsockopt(udpFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(udpRealPort ? udpRealPort : port);
  if (bind(udpFd, (struct sockaddr *)&addr, sizeof(addr)) != 0) { close(udpFd); udpFd = -1; return false; }
  return true;
#endif
}

bool udpReceive(Datagram &out) {
  if (!udpRx.empty()) { out = udpRx.front(); udpRx.pop_front(); return true; }
#ifndef __EMSCRIPTEN__
  if (udpFd >= 0) {
    uint8_t buf[2048];
    struct sockaddr_in from = {};
    socklen_t fromLen = sizeof(from);
    const ssize_t n = recvfrom(udpFd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromLen);
    if (n > 0) {
      out.data.assign(buf, buf + n);
      out.ip = from.sin_addr.s_addr;
      out.port = ntohs(from.sin_port);
      out.real = true;
      return true;
    }
  }
#endif
  return false;
}

void udpSend(const Datagram &d) {
#ifndef __EMSCRIPTEN__
  if (d.real && udpFd >= 0) {
    struct sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = d.ip;
    to.sin_port = htons(d.port);
    sendto(udpFd, d.data.data(), d.data.size(), 0, (struct sockaddr *)&to, sizeof(to));
    return;
  }
#endif
  udpTx.push_back(d.data);
  if (stopOnUdp) stopRequested = true;
}

void log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[sim %8.3f] ", vnow / 1e6);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

std::string fsRoot(const char *which) { return board.dataDir + "/" + which; }

}  // namespace sim

// ── The C library's clock, as the chip's clock ──────────────
extern "C" time_t time(time_t *out) __THROW {
  const time_t t = (time_t)(sim::rtcNowUs() / 1000000);
  if (out) *out = t;
  return t;
}
extern "C" int gettimeofday(struct timeval *tv, void *) __THROW {
  if (tv) {
    const int64_t us = sim::rtcNowUs();
    tv->tv_sec = us / 1000000;
    tv->tv_usec = us % 1000000;
  }
  return 0;
}

// ── Control channel ─────────────────────────────────────────
using namespace sim;

static std::string hexEncode(const uint8_t *data, size_t len) {
  static const char *digits = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) { out += digits[data[i] >> 4]; out += digits[data[i] & 15]; }
  return out;
}
static std::string hexDecode(const std::string &hex) {
  std::string out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) out += (char)strtol(hex.substr(i, 2).c_str(), nullptr, 16);
  return out;
}
static std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at < line.size()) {
    while (at < line.size() && isspace((unsigned char)line[at])) at++;
    size_t end = at;
    while (end < line.size() && !isspace((unsigned char)line[end])) end++;
    if (end > at) out.push_back(line.substr(at, end - at));
    at = end;
  }
  return out;
}
static std::string arg(const std::vector<std::string> &a, size_t i) { return i < a.size() ? a[i] : std::string(); }

static void loopTask(void *) {
  setup();
  for (;;) loop();
}

static void saveShot(const char *path, int *w, int *h);
void simPanelView(std::vector<uint16_t> &out, int &w, int &h);
std::string simStateJson();

static void saveShot(const char *path, int *w, int *h) {
  std::vector<uint16_t> view;
  simPanelView(view, *w, *h);
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fwrite(view.data(), 2, view.size(), f);
  fclose(f);
}

#ifdef __EMSCRIPTEN__
static std::string lastReply;            // read by the page after mbsim_cmd()
#endif
static void reply(const std::string &body) {
#ifdef __EMSCRIPTEN__
  char head[160];
  snprintf(head, sizeof(head), "\"t_ms\":%llu,\"true_ms\":%lld,\"rtc_ms\":%lld,\"restart\":%s,\"reason\":%d}",
           (unsigned long long)(vnow / 1000), (long long)(trueNowUs() / 1000), (long long)(rtcNowUs() / 1000),
           restartPending ? "true" : "false", restartReason);
  lastReply = "{" + body + (body.empty() ? "" : ",") + head;
  return;
#endif
  printf("{%s%s\"t_ms\":%llu,\"true_ms\":%lld,\"rtc_ms\":%lld,\"restart\":%s,\"reason\":%d}\n", body.c_str(), body.empty() ? "" : ",",
         (unsigned long long)(vnow / 1000), (long long)(trueNowUs() / 1000), (long long)(rtcNowUs() / 1000),
         restartPending ? "true" : "false", restartReason);
  fflush(stdout);
}

static void crashHandler(int sig) {
  char msg[96];
  const int n = snprintf(msg, sizeof(msg), "{\"event\":\"crash\",\"signal\":%d}\n", sig);
  ssize_t w = ::write(1, msg, (size_t)n);
  (void)w;
  _exit(70);
}

static bool handle(const std::string &line) {
  const std::vector<std::string> a = split(line);
  if (a.empty()) return true;
  const std::string &cmd = a[0];
  if (cmd == "run") {
    // run MS [udp] [serial HEX]: stops early at an answer on the link, or when the serial output holds the text
    stopOnUdp = false;
    stopOnSerial.clear();
    for (size_t i = 2; i < a.size(); i++) {
      if (a[i] == "udp") stopOnUdp = true;
      if (a[i] == "serial") { stopOnSerial = hexDecode(arg(a, i + 1)); serialScanFrom = serialTx.size(); }
    }
    const bool early = stopOnUdp || !stopOnSerial.empty();
    runUntil(vnow + strtoull(arg(a, 1).c_str(), nullptr, 10) * 1000ULL);
    const bool stopped = early && stopRequested;
    stopOnUdp = false;
    stopOnSerial.clear();
    reply(std::string("\"ok\":true,\"stopped\":") + (stopped ? "true" : "false"));
  } else if (cmd == "serial_in") {
    for (char c : hexDecode(arg(a, 1))) serialRx.push_back((uint8_t)c);
    reply("\"ok\":true");
  } else if (cmd == "serial_out") {
    const std::string hex = hexEncode((const uint8_t *)serialTx.data(), serialTx.size());
    serialTx.clear();
    serialScanFrom = 0;
    reply("\"ok\":true,\"hex\":\"" + hex + "\"");
  } else if (cmd == "udp") {
    Datagram d;
    const std::string bytes = hexDecode(arg(a, 1));
    d.data.assign(bytes.begin(), bytes.end());
    d.ip = htonl(INADDR_LOOPBACK);
    d.port = 40000;
    d.real = false;
    udpRx.push_back(d);
    reply("\"ok\":true");
  } else if (cmd == "udp_out") {
    std::string list;
    for (auto &p : udpTx) list += (list.empty() ? "\"" : ",\"") + hexEncode(p.data(), p.size()) + "\"";
    udpTx.clear();
    reply("\"ok\":true,\"packets\":[" + list + "]");
  } else if (cmd == "shot") {
    int w = 0, h = 0;
    saveShot(arg(a, 1).c_str(), &w, &h);
    char b[64];
    snprintf(b, sizeof(b), "\"ok\":true,\"w\":%d,\"h\":%d", w, h);
    reply(b);
  } else if (cmd == "touch") {
    board.touchDown = true;
    board.touchX = atoi(arg(a, 1).c_str());
    board.touchY = atoi(arg(a, 2).c_str());
    reply("\"ok\":true");
  } else if (cmd == "release") {
    board.touchDown = false;
    reply("\"ok\":true");
  } else if (cmd == "boot") {
    board.bootPressed = atoi(arg(a, 1).c_str()) != 0;
    reply("\"ok\":true");
  } else if (cmd == "accel") {
    for (int i = 0; i < 3; i++) board.accel[i] = strtof(arg(a, 1 + i).c_str(), nullptr);
    reply("\"ok\":true");
  } else if (cmd == "gyro") {
    for (int i = 0; i < 3; i++) board.gyro[i] = strtof(arg(a, 1 + i).c_str(), nullptr);
    reply("\"ok\":true");
  } else if (cmd == "ap_add") {
    const std::string ssid = hexDecode(arg(a, 1));
    for (auto it = board.aps.begin(); it != board.aps.end();) it = it->ssid == ssid ? board.aps.erase(it) : it + 1;
    board.aps.push_back({ssid, hexDecode(arg(a, 2)), a.size() > 3 ? atoi(a[3].c_str()) : -55});
    reply("\"ok\":true");
  } else if (cmd == "ap_del") {
    const std::string ssid = hexDecode(arg(a, 1));
    for (auto it = board.aps.begin(); it != board.aps.end();) it = it->ssid == ssid ? board.aps.erase(it) : it + 1;
    reply("\"ok\":true");
  } else if (cmd == "ap_clear") {
    board.aps.clear();
    reply("\"ok\":true");
  } else if (cmd == "set") {
    const std::string key = arg(a, 1), value = arg(a, 2);
    bool ok = true;
    if (key == "internet") board.internet = value != "0";
    else if (key == "card") board.card = value;
    else if (key == "ap_clients") board.apClients = atoi(value.c_str());
    else if (key == "touch_present") board.touchPresent = value != "0";
    else if (key == "imu_present") board.imuPresent = value != "0";
    else if (key == "crash_dump") board.crashDump = value != "0";
    else if (key == "http_mock") board.httpMock = value == "none" ? "" : value;
    else ok = false;
    reply(ok ? "\"ok\":true" : "\"ok\":false,\"error\":\"unknown key\"");
  } else if (cmd == "portal_save") {
    if (AyresWiFiManager::instance) AyresWiFiManager::instance->simPortalSave(hexDecode(arg(a, 1)).c_str(), hexDecode(arg(a, 2)).c_str());
    reply("\"ok\":true");
  } else if (cmd == "portal_erase") {
    if (AyresWiFiManager::instance) AyresWiFiManager::instance->simPortalErase();
    reply("\"ok\":true");
  } else if (cmd == "portal_hit") {
    if (AyresWiFiManager::instance) AyresWiFiManager::instance->simPortalRequest();
    reply("\"ok\":true");
  } else if (cmd == "power_cut") {
    restartPending = true;
    restartReason = 1;
    reply("\"ok\":true");
  } else if (cmd == "state") {
    reply("\"ok\":true," + simStateJson());
  } else if (cmd == "quit") {
    reply("\"ok\":true");
    return false;
  } else {
    reply("\"ok\":false,\"error\":\"unknown command\"");
  }
  return !restartPending;
}

static void usage() {
  fprintf(stderr,
          "mbsim: the Monitor Buddy firmware on a simulated board\n"
          "  --data DIR          the board's flash and card (littlefs/, sd/, ota.state); default ./mbsim-data\n"
          "  --epoch SEC         the time out in the world at boot (default: now)\n"
          "  --rtc SEC           what the chip's clock says at boot (default 0: never set)\n"
          "  --reset-reason N    esp_reset_reason(): 1 power-on, 3 restart, 4 crash ...\n"
          "  --seed N            seed of the board's random numbers\n"
          "  --ap SSID:PASS[:RSSI]  a Wi-Fi network in range (repeat)\n"
          "  --no-internet       the router has no internet\n"
          "  --http-mock H:P     the mock internet the firmware's HTTP requests go to\n"
          "  --card none|ok|nofat   the TF card\n"
          "  --no-imu, --no-touch   a board whose chip does not answer\n"
          "  --accel X,Y,Z       the pull at rest, in g (default 0,-1,0: standing in its holder)\n"
          "  --build-id HEX      the running image's ELF SHA-256\n"
          "  --crash-dump        a crash dump sits in flash\n"
          "  --uptime-ms N       start the millisecond clock there (to reach the 49.7-day wrap)\n"
          "  --pace F            virtual seconds per real second; 0 = as fast as it goes (default)\n"
          "  --udp-port N        take link packets on a real UDP socket at 127.0.0.1:N\n"
          "  --pty               give the USB serial port a pseudo-terminal (its path goes to stderr)\n"
          "  --serial-log FILE   append the serial output there\n"
          "  --echo              copy the serial output to stderr\n"
          "  --live              run by itself at --pace 1 until stdin closes\n"
          "Commands come on stdin, one a line; every answer is one line of JSON on stdout.\n");
}

int simBuildIdSet(const char *hex);
void simBoot();

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IOLBF, 0);
  board.dataDir = "./mbsim-data";
  struct timespec real;
  clock_gettime(CLOCK_REALTIME, &real);
  int64_t epoch = real.tv_sec, rtc = 0;
  bool live = false, wantPty = false;
  unsigned long seed = 1;
  for (int i = 1; i < argc; i++) {
    const std::string k = argv[i];
    const char *v = i + 1 < argc ? argv[i + 1] : "";
    if (k == "--data") { board.dataDir = v; i++; }
    else if (k == "--epoch") { epoch = strtoll(v, nullptr, 10); i++; }
    else if (k == "--rtc") { rtc = strtoll(v, nullptr, 10); i++; }
    else if (k == "--reset-reason") { board.resetReason = atoi(v); i++; }
    else if (k == "--seed") { seed = strtoul(v, nullptr, 10); i++; }
    else if (k == "--ap") {
      std::string s = v; i++;
      const size_t c1 = s.find(':'), c2 = c1 == std::string::npos ? c1 : s.find(':', c1 + 1);
      Ap ap;
      ap.ssid = s.substr(0, c1);
      ap.password = c1 == std::string::npos ? "" : s.substr(c1 + 1, c2 == std::string::npos ? c2 : c2 - c1 - 1);
      ap.rssi = c2 == std::string::npos ? -55 : atoi(s.substr(c2 + 1).c_str());
      board.aps.push_back(ap);
    }
    else if (k == "--no-internet") board.internet = false;
    else if (k == "--http-mock") { board.httpMock = v; i++; }
    else if (k == "--card") { board.card = v; i++; }
    else if (k == "--no-imu") board.imuPresent = false;
    else if (k == "--no-touch") board.touchPresent = false;
    else if (k == "--accel") { sscanf(v, "%f,%f,%f", &board.accel[0], &board.accel[1], &board.accel[2]); i++; }
    else if (k == "--build-id") { simBuildIdSet(v); i++; }
    else if (k == "--crash-dump") board.crashDump = true;
    else if (k == "--uptime-ms") { vnow = strtoull(v, nullptr, 10) * 1000ULL; i++; }
    else if (k == "--pace") { pace = strtod(v, nullptr); i++; }
    else if (k == "--udp-port") { udpRealPort = (uint16_t)atoi(v); i++; }
    else if (k == "--pty") wantPty = true;
    else if (k == "--serial-log") { serialLog = fopen(v, "ab"); i++; }
    else if (k == "--echo") serialEcho = true;
    else if (k == "--live") live = true;
    else { usage(); return k == "--help" || k == "-h" ? 0 : 2; }
  }
  if (live && pace <= 0) pace = 1;
  mkdir(board.dataDir.c_str(), 0755);
  mkdir(fsRoot("littlefs").c_str(), 0755);
  trueEpochAtBootUs = epoch * 1000000LL - (int64_t)vnow;
  rtcEpochAtBootUs = rtc * 1000000LL - (int64_t)vnow;
  paceVirtualStart = vnow;
  clock_gettime(CLOCK_MONOTONIC, &paceRealStart);
  randomSeed(seed);
#ifndef __EMSCRIPTEN__
  if (wantPty) {
    int slave = -1;
    char name[128];
    struct termios raw;
    if (openpty(&ptyFd, &slave, name, nullptr, nullptr) == 0) {
      tcgetattr(slave, &raw);
      cfmakeraw(&raw);
      tcsetattr(slave, TCSANOW, &raw);
      fcntl(ptyFd, F_SETFL, fcntl(ptyFd, F_GETFL) | O_NONBLOCK);
      fprintf(stderr, "mbsim: serial port %s\n", name);
      const std::string link = board.dataDir + "/serial";
      unlink(link.c_str());
      if (symlink(name, link.c_str()) != 0) fprintf(stderr, "mbsim: no link %s\n", link.c_str());
    }
  }
  signal(SIGSEGV, crashHandler);
  signal(SIGABRT, crashHandler);
  signal(SIGFPE, crashHandler);
  signal(SIGBUS, crashHandler);
  signal(SIGPIPE, SIG_IGN);
#else
  (void)wantPty; (void)live;
  emscripten_fiber_init_from_current_context(&mainCtx, mainUnwind, sizeof(mainUnwind));
#endif
  simBoot();
  taskCreate(loopTask, nullptr, "loopTask", 1);
#ifdef __EMSCRIPTEN__
  return 0;   // the page goes on with mbsim_cmd()
#endif

  char *line = nullptr;
  size_t cap = 0;
  if (live) {
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    std::string pending;
    bool open = true, go = true;
    while (go && !restartPending) {
      runUntil(vnow + 20000);
      char buf[4096];
      const ssize_t n = open ? ::read(0, buf, sizeof(buf)) : -1;
      if (n == 0) open = false;
      if (n > 0) pending.append(buf, (size_t)n);
      size_t nl;
      while (go && (nl = pending.find('\n')) != std::string::npos) {
        go = handle(pending.substr(0, nl));
        pending.erase(0, nl + 1);
      }
    }
    if (restartPending) reply("\"event\":\"restart\"");
  } else {
    while (getline(&line, &cap, stdin) > 0) {
      if (!handle(line)) break;
    }
  }
  free(line);
  return 0;
}

#ifdef __EMSCRIPTEN__
// The page's side of the control channel: one command in, then mbsim_reply()
// for its line of JSON. (A command that runs the tasks switches fibers, and
// what the call itself returns is lost on the way back: hence the second call.)
extern "C" {
EMSCRIPTEN_KEEPALIVE void mbsim_cmd(const char *line) {
  // every call comes in on a new piece of stack: tell the scheduler's fiber
  emscripten_fiber_init_from_current_context(&mainCtx, mainUnwind, sizeof(mainUnwind));
  lastReply.clear();
  handle(line);
}
EMSCRIPTEN_KEEPALIVE const char *mbsim_reply() { return lastReply.c_str(); }
// The glass as RGBA, for a canvas: returns the pixels, w and h through the pointers.
EMSCRIPTEN_KEEPALIVE const uint8_t *mbsim_frame(int *w, int *h) {
  static std::vector<uint16_t> view;
  static std::vector<uint8_t> rgba;
  simPanelView(view, *w, *h);
  rgba.resize(view.size() * 4);
  for (size_t i = 0; i < view.size(); i++) {
    const uint16_t c = view[i];
    const uint8_t r = c >> 11, g = (c >> 5) & 63, b = c & 31;
    rgba[i * 4] = (uint8_t)(r * 255 / 31);        // as Pillow widens RGB565: the golden pictures' numbers
    rgba[i * 4 + 1] = (uint8_t)(g * 255 / 63);
    rgba[i * 4 + 2] = (uint8_t)(b * 255 / 31);
    rgba[i * 4 + 3] = 255;
  }
  return rgba.data();
}
}
#endif
