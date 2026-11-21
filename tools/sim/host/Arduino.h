// Arduino core for the host build of the Monitor Buddy firmware (tools/sim).
// Only what src/main.cpp and the libraries it uses need.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <algorithm>
#include <string>
#include "sim.h"

#define ARDUINO 10812
#define MBSIM 1

using std::min;
using std::max;
using std::isnan;
using std::isinf;

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define PI 3.1415926535897932384626433832795
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bit(b) (1UL << (b))
#define _BV(b) (1UL << (b))
#define lowByte(w) ((uint8_t)((w) & 0xff))
#define highByte(w) ((uint8_t)((w) >> 8))

#define PROGMEM
#define PGM_P const char *
#define PSTR(s) (s)
class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))
#define pgm_read_pointer(addr) (*(void *const *)(addr))
#define memcpy_P memcpy
#define strlen_P strlen
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define IRAM_ATTR
#define RTC_DATA_ATTR
#define ICACHE_RAM_ATTR

#if defined(__GLIBC__)
#if !__GLIBC_PREREQ(2, 38)   // newer glibc and Emscripten's libc have their own
#define MBSIM_OWN_STRLCPY 1
#endif
#endif
#ifdef MBSIM_OWN_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
#endif

// 32 bits, as on the chip: `millis() - stamp` must wrap the way it does there
// (unsigned long is 64 bits on a PC).
uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield();
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);
long map(long x, long in_min, long in_max, long out_min, long out_max);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int  digitalRead(uint8_t pin);
bool ledcAttach(uint8_t pin, uint32_t freq, uint8_t resolution);
bool ledcWrite(uint8_t pin, uint32_t duty);
int  analogRead(uint8_t pin);

bool getLocalTime(struct tm *info, uint32_t ms = 5000);
void configTime(long gmtOffset_sec, int daylightOffset_sec, const char *server1, const char *server2 = nullptr, const char *server3 = nullptr);
void configTzTime(const char *tz, const char *server1, const char *server2 = nullptr, const char *server3 = nullptr);

// ── String ──────────────────────────────────────────────────
class String {
 public:
  String() {}
  String(const char *s) { if (s) v = s; }
  String(const __FlashStringHelper *s) { if (s) v = reinterpret_cast<const char *>(s); }
  String(const char *s, size_t n) { if (s) v.assign(s, n); }
  String(const std::string &s) : v(s) {}
  String(const String &) = default;
  String(String &&) = default;
  explicit String(char c) : v(1, c) {}
  explicit String(unsigned char n, unsigned char base = 10) { fromInt((long long)n, base); }
  explicit String(int n, unsigned char base = 10) { fromInt(n, base); }
  explicit String(unsigned int n, unsigned char base = 10) { fromInt((long long)n, base); }
  explicit String(long n, unsigned char base = 10) { fromInt(n, base); }
  explicit String(unsigned long n, unsigned char base = 10) { fromUInt(n, base); }
  explicit String(long long n, unsigned char base = 10) { fromInt(n, base); }
  explicit String(unsigned long long n, unsigned char base = 10) { fromUInt(n, base); }
  explicit String(float n, unsigned int decimals = 2) { fromDouble(n, decimals); }
  explicit String(double n, unsigned int decimals = 2) { fromDouble(n, decimals); }
  String &operator=(const String &) = default;
  String &operator=(String &&) = default;
  String &operator=(const char *s) { v = s ? s : ""; return *this; }

  const char *c_str() const { return v.c_str(); }
  unsigned int length() const { return (unsigned int)v.size(); }
  bool isEmpty() const { return v.empty(); }
  bool reserve(unsigned int n) { v.reserve(n); return true; }
  void clear() { v.clear(); }

  bool concat(const String &s) { v += s.v; return true; }
  bool concat(const char *s) { if (s) v += s; return true; }
  bool concat(const char *s, unsigned int n) { if (s) v.append(s, n); return true; }
  bool concat(char c) { v += c; return true; }
  bool concat(unsigned char n) { return concat(String(n)); }
  bool concat(int n) { return concat(String(n)); }
  bool concat(unsigned int n) { return concat(String(n)); }
  bool concat(long n) { return concat(String(n)); }
  bool concat(unsigned long n) { return concat(String(n)); }
  bool concat(long long n) { return concat(String(n)); }
  bool concat(unsigned long long n) { return concat(String(n)); }
  bool concat(float n) { return concat(String(n)); }
  bool concat(double n) { return concat(String(n)); }
  template <typename T> String &operator+=(const T &x) { concat(x); return *this; }

  char charAt(unsigned int i) const { return i < v.size() ? v[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }
  char &operator[](unsigned int i) { static char dummy; return i < v.size() ? v[i] : (dummy = 0, dummy); }

  bool equals(const String &s) const { return v == s.v; }
  bool equals(const char *s) const { return v == (s ? s : ""); }
  bool equalsIgnoreCase(const String &s) const { return strcasecmp(c_str(), s.c_str()) == 0; }
  bool operator==(const String &s) const { return v == s.v; }
  bool operator==(const char *s) const { return equals(s); }
  bool operator!=(const String &s) const { return v != s.v; }
  bool operator!=(const char *s) const { return !equals(s); }
  bool operator<(const String &s) const { return v < s.v; }
  int compareTo(const String &s) const { return v.compare(s.v); }
  bool startsWith(const String &p) const { return v.compare(0, p.v.size(), p.v) == 0; }
  bool startsWith(const String &p, unsigned int off) const { return off <= v.size() && v.compare(off, p.v.size(), p.v) == 0; }
  bool endsWith(const String &s) const { return v.size() >= s.v.size() && v.compare(v.size() - s.v.size(), s.v.size(), s.v) == 0; }
  int indexOf(char c, unsigned int from = 0) const { size_t p = v.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String &s, unsigned int from = 0) const { size_t p = v.find(s.v, from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = v.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(const String &s) const { size_t p = v.rfind(s.v); return p == std::string::npos ? -1 : (int)p; }
  String substring(unsigned int from) const { return from >= v.size() ? String() : String(v.substr(from)); }
  String substring(unsigned int from, unsigned int to) const {
    if (from > to) std::swap(from, to);
    if (from >= v.size()) return String();
    return String(v.substr(from, to - from));
  }
  void replace(const String &find, const String &with) {
    if (find.v.empty()) return;
    size_t at = 0;
    while ((at = v.find(find.v, at)) != std::string::npos) { v.replace(at, find.v.size(), with.v); at += with.v.size(); }
  }
  void replace(char find, char with) { for (auto &c : v) if (c == find) c = with; }
  void remove(unsigned int index) { if (index < v.size()) v.erase(index); }
  void remove(unsigned int index, unsigned int count) { if (index < v.size()) v.erase(index, count); }
  void toLowerCase() { for (auto &c : v) c = (char)tolower((unsigned char)c); }
  void toUpperCase() { for (auto &c : v) c = (char)toupper((unsigned char)c); }
  void trim() {
    size_t a = 0, b = v.size();
    while (a < b && isspace((unsigned char)v[a])) a++;
    while (b > a && isspace((unsigned char)v[b - 1])) b--;
    v = v.substr(a, b - a);
  }
  long toInt() const { return strtol(v.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(v.c_str(), nullptr); }
  double toDouble() const { return strtod(v.c_str(), nullptr); }
  void toCharArray(char *buf, unsigned int size, unsigned int index = 0) const {
    if (!size) return;
    if (index >= v.size()) { buf[0] = 0; return; }
    size_t n = std::min((size_t)size - 1, v.size() - index);
    memcpy(buf, v.data() + index, n);
    buf[n] = 0;
  }
  void getBytes(unsigned char *buf, unsigned int size, unsigned int index = 0) const { toCharArray((char *)buf, size, index); }
  const char *begin() const { return v.c_str(); }
  const char *end() const { return v.c_str() + v.size(); }
  std::string v;

 private:
  void fromInt(long long n, unsigned char base) {
    if (base == 10) { v = std::to_string(n); return; }
    fromUInt((unsigned long long)n, base);
  }
  void fromUInt(unsigned long long n, unsigned char base) {
    if (base == 10) { v = std::to_string(n); return; }
    if (n == 0) { v = "0"; return; }
    while (n) { v.insert(v.begin(), "0123456789abcdef"[n % base]); n /= base; }
  }
  void fromDouble(double n, unsigned int decimals) { char b[64]; snprintf(b, sizeof(b), "%.*f", (int)decimals, n); v = b; }
};
inline String operator+(const String &a, const String &b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, const char *b) { String r(a); r.concat(b); return r; }
inline String operator+(const char *a, const String &b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, char b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, unsigned int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, unsigned long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, float b) { String r(a); r.concat(b); return r; }
inline String operator+(const String &a, double b) { String r(a); r.concat(b); return r; }
inline bool operator==(const char *a, const String &b) { return b.equals(a); }

// ── Print, Stream ───────────────────────────────────────────
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

class Print;
class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print &p) const = 0;
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buffer, size_t size) {
    size_t n = 0;
    while (size--) n += write(*buffer++);
    return n;
  }
  size_t write(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
  size_t write(const char *s, size_t n) { return write((const uint8_t *)s, n); }
  virtual int availableForWrite() { return 0; }
  virtual void flush() {}
  int getWriteError() { return 0; }
  void clearWriteError() {}

  size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap, copy;
    va_start(ap, format);
    va_copy(copy, ap);
    int n = vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (n < 0) { va_end(ap); return 0; }
    std::string buf((size_t)n + 1, '\0');
    vsnprintf(&buf[0], buf.size(), format, ap);
    va_end(ap);
    return write((const uint8_t *)buf.data(), (size_t)n);
  }
  size_t print(const String &s) { return write((const uint8_t *)s.c_str(), s.length()); }
  size_t print(const char *s) { return write(s); }
  size_t print(const __FlashStringHelper *s) { return write(reinterpret_cast<const char *>(s)); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(int n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(unsigned int n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(long n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(unsigned long n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(long long n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(unsigned long long n, int base = DEC) { return print(String(n, (unsigned char)base)); }
  size_t print(double n, int digits = 2) { return print(String(n, (unsigned int)digits)); }
  size_t print(const Printable &p) { return p.printTo(*this); }
  size_t println() { return write("\r\n"); }
  template <typename T> size_t println(const T &x) { size_t n = print(x); return n + println(); }
  template <typename T> size_t println(const T &x, int arg) { size_t n = print(x, arg); return n + println(); }
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  void setTimeout(unsigned long ms) { timeoutMs = ms; }
  unsigned long getTimeout() const { return timeoutMs; }
  virtual size_t readBytes(char *buffer, size_t length) {
    size_t n = 0;
    while (n < length) {
      int c = read();
      if (c < 0) break;
      buffer[n++] = (char)c;
    }
    return n;
  }
  size_t readBytes(uint8_t *buffer, size_t length) { return readBytes((char *)buffer, length); }
  virtual String readString() {
    String s;
    for (int c = read(); c >= 0; c = read()) s.concat((char)c);
    return s;
  }
  String readStringUntil(char end) {
    String s;
    for (int c = read(); c >= 0 && c != end; c = read()) s.concat((char)c);
    return s;
  }
 protected:
  unsigned long timeoutMs = 1000;
};

// ── IPAddress ───────────────────────────────────────────────
class IPAddress : public Printable {
 public:
  IPAddress() : a(0) {}
  IPAddress(uint8_t o1, uint8_t o2, uint8_t o3, uint8_t o4) : a((uint32_t)o1 | (uint32_t)o2 << 8 | (uint32_t)o3 << 16 | (uint32_t)o4 << 24) {}
  IPAddress(uint32_t address) : a(address) {}
  operator uint32_t() const { return a; }
  uint8_t operator[](int i) const { return (uint8_t)(a >> (8 * i)); }
  bool operator==(const IPAddress &o) const { return a == o.a; }
  String toString() const {
    char b[20];
    snprintf(b, sizeof(b), "%u.%u.%u.%u", (unsigned)(a & 255), (unsigned)(a >> 8 & 255), (unsigned)(a >> 16 & 255), (unsigned)(a >> 24));
    return String(b);
  }
  size_t printTo(Print &p) const override { return p.print(toString()); }
  bool fromString(const char *s) {
    unsigned o[4];
    if (sscanf(s, "%u.%u.%u.%u", &o[0], &o[1], &o[2], &o[3]) != 4) return false;
    a = o[0] | o[1] << 8 | o[2] << 16 | o[3] << 24;
    return true;
  }
 private:
  uint32_t a;
};

// ── Serial: the chip's USB-Serial-JTAG ──────────────────────
class HWCDC : public Stream {
 public:
  void begin(unsigned long = 0) {}
  void end() {}
  void setRxBufferSize(size_t) {}
  void setTxBufferSize(size_t) {}
  void setTxTimeoutMs(uint32_t) {}
  void setDebugOutput(bool) {}
  int available() override { return sim::serialInAvailable(); }
  int read() override { return sim::serialIn(); }
  int peek() override { return -1; }
  size_t write(uint8_t c) override { sim::serialOut(&c, 1); return 1; }
  size_t write(const uint8_t *b, size_t n) override { sim::serialOut(b, n); return n; }
  using Print::write;
  operator bool() const { return true; }
};
extern HWCDC Serial;

// ── SPI (only handed around) ────────────────────────────────
class SPIClass {
 public:
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end() {}
};
extern SPIClass SPI;

// ── ESP ─────────────────────────────────────────────────────
class EspClass {
 public:
  [[noreturn]] void restart();
  uint32_t getFreeHeap() { return 214000; }
  uint32_t getMinFreeHeap() { return 171000; }
  uint32_t getHeapSize() { return 320000; }
  uint32_t getMaxAllocHeap() { return 110000; }
  uint32_t getFlashChipSize() { return 8 * 1024 * 1024; }
  const char *getChipModel() { return "ESP32-C6 (simulated)"; }
  uint64_t getEfuseMac();
};
extern EspClass ESP;

// ── FreeRTOS, as much as the firmware uses ──────────────────
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *TaskHandle_t;
typedef sim::Mutex *SemaphoreHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0xFFFFFFFFUL
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define taskENTER_CRITICAL(mux) ((void)(mux))
#define taskEXIT_CRITICAL(mux) ((void)(mux))
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define taskYIELD() yield()
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *handle);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *handle, int core);
void vTaskDelay(TickType_t ticks);
BaseType_t vTaskDelayUntil(TickType_t *previousWake, TickType_t increment);
TickType_t xTaskGetTickCount();
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
void vTaskDelete(TaskHandle_t task);
SemaphoreHandle_t xSemaphoreCreateMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t m);

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
