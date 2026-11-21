// Monitor Buddy simulator: the board. The Arduino core calls, the pins, the
// I2C bus with its touch controller and motion sensor, and the LCD panel
// behind the SPI bus.
#include "sim.h"
#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_core_dump.h>

using namespace sim;

HWCDC Serial;
SPIClass SPI;
EspClass ESP;
TwoWire Wire;

#if defined(__GLIBC__)
#if !__GLIBC_PREREQ(2, 38)   // newer glibc and Emscripten's libc have their own
#define MBSIM_OWN_STRLCPY 1
#endif
#endif
#ifdef MBSIM_OWN_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t size) {
  const size_t len = strlen(src);
  if (size) { const size_t n = len >= size ? size - 1 : len; memcpy(dst, src, n); dst[n] = 0; }
  return len;
}
size_t strlcat(char *dst, const char *src, size_t size) {
  const size_t have = strnlen(dst, size);
  if (have == size) return size + strlen(src);
  return have + strlcpy(dst + have, src, size - have);
}
#endif

// ── Time ────────────────────────────────────────────────────
uint32_t millis() { spin(); return (uint32_t)(nowUs() / 1000ULL); }
uint32_t micros() { spin(); return (uint32_t)nowUs(); }
void delay(uint32_t ms) { sleepUs((uint64_t)ms * 1000ULL); }
void delayMicroseconds(uint32_t us) { sleepUs(us); }
void yield() { sleepUs(0); }

// ── Random numbers: the same run every time for the same seed ──
static uint64_t rngState = 0x9E3779B97F4A7C15ULL;
static uint32_t rngNext() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 7;
  rngState ^= rngState << 17;
  return (uint32_t)(rngState >> 16);
}
void randomSeed(unsigned long seed) { rngState = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)seed * 0xD1B54A32D192ED03ULL); if (!rngState) rngState = 1; }
long random(long howbig) { return howbig <= 0 ? 0 : (long)(rngNext() % (uint32_t)howbig); }
long random(long howsmall, long howbig) { return howsmall >= howbig ? howsmall : random(howbig - howsmall) + howsmall; }
long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return in_max == in_min ? out_min : (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
uint32_t esp_random() { return rngNext(); }

// ── Pins ────────────────────────────────────────────────────
static uint8_t pinLevel[64];
static uint8_t pinModes[64];
void pinMode(uint8_t pin, uint8_t mode) { if (pin < 64) { pinModes[pin] = mode; if (mode == INPUT_PULLUP) pinLevel[pin] = HIGH; } }
void digitalWrite(uint8_t pin, uint8_t value) { if (pin < 64) pinLevel[pin] = value ? HIGH : LOW; }
int digitalRead(uint8_t pin) {
  if (pin == 9) return board.bootPressed ? LOW : HIGH;   // BOOT, pressed = low
  return pin < 64 ? pinLevel[pin] : HIGH;
}
int analogRead(uint8_t) { return 0; }
bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
bool ledcWrite(uint8_t pin, uint32_t duty) {
  if (pin == 23) board.backlight = (int)duty;   // LCD_BL
  return true;
}

// ── FreeRTOS ────────────────────────────────────────────────
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t, void *arg, UBaseType_t priority, TaskHandle_t *handle) {
  if (handle) *handle = nullptr;
  return taskCreate(fn, arg, name, (int)priority) ? pdPASS : pdFAIL;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *handle, int) {
  return xTaskCreate(fn, name, stack, arg, priority, handle);
}
void vTaskDelay(TickType_t ticks) { sleepUs((uint64_t)ticks * 1000ULL); }
TickType_t xTaskGetTickCount() { return (TickType_t)(nowUs() / 1000ULL); }
BaseType_t vTaskDelayUntil(TickType_t *previousWake, TickType_t increment) {
  const TickType_t now = xTaskGetTickCount(), due = *previousWake + increment;
  *previousWake = due;
  if ((int32_t)(due - now) > 0) {
    // to the tick itself, whatever part of a millisecond the task used
    const uint64_t dueUs = (nowUs() / 1000ULL + (due - now)) * 1000ULL;
    sleepUs(dueUs - nowUs());
    return pdTRUE;
  }
  sleepUs(0);
  return pdFALSE;
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 8400; }
void vTaskDelete(TaskHandle_t) { for (;;) sleepUs(3600ULL * 1000000ULL); }
SemaphoreHandle_t xSemaphoreCreateMutex() { return mutexNew(); }
BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t ticks) {
  return mutexTake(m, ticks == portMAX_DELAY ? UINT64_MAX : (uint64_t)ticks * 1000ULL) ? pdTRUE : pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t m) { mutexGive(m); return pdTRUE; }

// ── ESP ─────────────────────────────────────────────────────
void EspClass::restart() { sim::restart(3, "ESP.restart"); }
uint64_t EspClass::getEfuseMac() { uint64_t m = 0; memcpy(&m, board.mac, 6); return m; }
esp_reset_reason_t esp_reset_reason() { return (esp_reset_reason_t)board.resetReason; }
void esp_restart() { sim::restart(3, "esp_restart"); }
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t) { memcpy(mac, board.mac, 6); return ESP_OK; }
esp_err_t esp_core_dump_image_check() { return board.crashDump ? ESP_OK : ESP_FAIL; }

// ── I2C: AXS5106L touch (0x63) and QMI8658 motion sensor (0x6B) ──
static uint8_t imuReg[256];
static uint8_t touchReg = 0;
static uint8_t imuPointer = 0;

static float imuAccelScale() {   // CTRL2 bits 6:4
  switch ((imuReg[0x03] >> 4) & 7) { case 0: return 2.0f; case 1: return 4.0f; case 2: return 8.0f; default: return 16.0f; }
}
static float imuGyroScale() {    // CTRL3 bits 6:4
  static const float dps[8] = {16, 32, 64, 128, 256, 512, 1024, 2048};
  return dps[(imuReg[0x04] >> 4) & 7];
}
static int16_t toRaw(float value, float fullScale) {
  const float raw = value / fullScale * 32768.0f;
  return (int16_t)(raw > 32767.0f ? 32767 : raw < -32768.0f ? -32768 : lroundf(raw));
}
static uint8_t imuRead(uint8_t reg) {
  if (reg == 0x00) return 0x05;                      // WHO_AM_I
  if (reg == 0x01) return 0x7C;                      // REVISION_ID
  if (reg == 0x2E) return (imuReg[0x08] & 0x03);     // STATUS0: data ready for what CTRL7 enabled
  if (reg >= 0x35 && reg <= 0x40) {
    const int i = (reg - 0x35) / 2;
    const bool enabled = i < 3 ? (imuReg[0x08] & 1) : (imuReg[0x08] & 2);
    const int16_t raw = !enabled ? 0 : i < 3 ? toRaw(board.accel[i], imuAccelScale()) : toRaw(board.gyro[i - 3], imuGyroScale());
    return (uint8_t)((reg - 0x35) % 2 ? raw >> 8 : raw & 0xFF);
  }
  return imuReg[reg];
}
static void imuWrite(uint8_t reg, uint8_t value) {
  if (reg == 0x60 && value == 0xFF) { memset(imuReg, 0, sizeof(imuReg)); return; }   // soft reset
  imuReg[reg] = value;
}

// The glass. The real board: an ST7789 with 240x320 of memory behind 172x320
// of glass (columns 34 to 205). `make SCREEN=WxH` builds another panel
// instead: H x W of glass with the same memory, which the firmware's sideways
// rotation turns into a W x H screen.
#ifdef MBSIM_SCREEN_W
static const int RAM_W = MBSIM_SCREEN_H, RAM_H = MBSIM_SCREEN_W, GLASS_X = 0, GLASS_W = MBSIM_SCREEN_H;
#else
static const int RAM_W = 240, RAM_H = 320, GLASS_X = 34, GLASS_W = 172;
#endif

// The controller's raw axes for a point on the screen: the firmware's mapping, undone.
static void touchRaw(uint16_t *rawX, uint16_t *rawY) {
  const int w = RAM_H, h = GLASS_W, edge = 3;
  auto unscale = [&](int out, int outMax) {
    const int rawMax = outMax - 1 - edge;
    int raw = edge + (int)lround((double)out * (rawMax - edge) / (outMax - 1));
    // the firmware rounds down: step up until its formula gives the point back
    while (raw < rawMax && (raw - edge) * (outMax - 1) / (rawMax - edge) < out) raw++;
    return raw;
  };
  int x = constrain(board.touchX, 0, w - 1), y = constrain(board.touchY, 0, h - 1);
  if (board.rotation == 3) { x = w - 1 - x; y = h - 1 - y; }
  *rawY = (uint16_t)unscale(x, w);
  *rawX = (uint16_t)unscale(y, h);
}

bool TwoWire::begin(int, int, uint32_t) { started = true; return true; }
bool TwoWire::end() { started = false; return true; }
bool TwoWire::setClock(uint32_t clock) { hz = clock ? clock : 100000; return true; }
void TwoWire::beginTransmission(uint8_t a) { address = a; txLen = 0; }
size_t TwoWire::write(uint8_t value) { if (txLen < sizeof(tx)) tx[txLen++] = value; return 1; }
static bool i2cAnswers(uint8_t address) {
  return (address == 0x6B && board.imuPresent) || (address == 0x63 && board.touchPresent);
}
uint8_t TwoWire::endTransmission(bool) {
  spendUs((uint64_t)(txLen + 1) * 9ULL * 1000000ULL / hz);
  if (!started) return 4;
  if (!i2cAnswers(address)) return 2;   // NACK on the address
  if (address == 0x6B && txLen >= 1) {
    imuPointer = tx[0];
    for (size_t i = 1; i < txLen; i++) imuWrite(imuPointer++, tx[i]);
  } else if (address == 0x63 && txLen >= 1) {
    touchReg = tx[0];
  }
  txLen = 0;
  return 0;
}
uint8_t TwoWire::requestFrom(uint8_t a, uint8_t count, uint8_t) {
  rxLen = rxAt = 0;
  spendUs((uint64_t)(count + 1) * 9ULL * 1000000ULL / hz);
  if (!started || !i2cAnswers(a)) return 0;
  if (count > sizeof(rx)) count = sizeof(rx);
  if (a == 0x6B) {
    for (uint8_t i = 0; i < count; i++) rx[i] = imuRead(imuPointer++);
  } else {
    memset(rx, 0, count);
    if (touchReg == 0x01 && board.touchDown && count >= 6) {
      uint16_t rawX, rawY;
      touchRaw(&rawX, &rawY);
      rx[1] = 1;
      rx[2] = rawX >> 8 & 0x0F; rx[3] = rawX & 0xFF;
      rx[4] = rawY >> 8 & 0x0F; rx[5] = rawY & 0xFF;
    }
  }
  rxLen = count;
  return count;
}
int TwoWire::available() { return (int)(rxLen - rxAt); }
int TwoWire::read() { return rxAt < rxLen ? rx[rxAt++] : -1; }

// ── The LCD: an ST7789-style controller with 240x320 of memory behind a
// 172x320 glass (columns 34 to 205) ──
static uint16_t panelRam[RAM_W * RAM_H];
static uint8_t  panelCmd = 0, panelMadctl = 0, panelArgs[8];
static int      panelArgCount = 0;
static int      winX0 = 0, winX1 = RAM_W - 1, winY0 = 0, winY1 = RAM_H - 1, curX = 0, curY = 0;
static bool     panelSleeping = true, panelDisplayOn = false, pixelHigh = true;
static uint8_t  pixelHighByte = 0;
static uint64_t busBytes = 0;

static void panelPixel(uint16_t color) {
  // the command's (x, y) to the memory's (column, row)
  int col = curX, row = curY;
  if (panelMadctl & 0x20) std::swap(col, row);
  if (panelMadctl & 0x40) col = RAM_W - 1 - col;
  if (panelMadctl & 0x80) row = RAM_H - 1 - row;
  if (col >= 0 && col < RAM_W && row >= 0 && row < RAM_H) panelRam[row * RAM_W + col] = color;
  board.flushedPixels++;
  if (++curX > winX1) { curX = winX0; if (++curY > winY1) curY = winY0; }
}
static void panelCommand(uint8_t c) {
  panelCmd = c;
  panelArgCount = 0;
  pixelHigh = true;
  switch (c) {
    case 0x01: panelSleeping = true; panelDisplayOn = false; panelMadctl = 0; break;   // SWRESET
    case 0x10: panelSleeping = true; break;
    case 0x11: panelSleeping = false; break;
    case 0x28: panelDisplayOn = false; break;
    case 0x29: panelDisplayOn = true; break;
    case 0x2C: curX = winX0; curY = winY0; board.flushes++; break;
  }
  board.panelOn = panelDisplayOn && !panelSleeping;
}
static void panelData(uint8_t d) {
  if (panelCmd == 0x2C) {
    if (pixelHigh) pixelHighByte = d; else panelPixel((uint16_t)(pixelHighByte << 8 | d));
    pixelHigh = !pixelHigh;
    return;
  }
  if (panelArgCount < 8) panelArgs[panelArgCount++] = d;
  if (panelCmd == 0x2A && panelArgCount == 4) { winX0 = panelArgs[0] << 8 | panelArgs[1]; winX1 = panelArgs[2] << 8 | panelArgs[3]; }
  if (panelCmd == 0x2B && panelArgCount == 4) { winY0 = panelArgs[0] << 8 | panelArgs[1]; winY1 = panelArgs[2] << 8 | panelArgs[3]; }
  if (panelCmd == 0x36 && panelArgCount == 1) {
    panelMadctl = d;
    board.rotation = !(d & 0x20) ? ((d & 0x80) ? 2 : 0) : ((d & 0x80) ? 3 : 1);
  }
}

bool Arduino_HWSPI::begin(int32_t speed, int8_t) {
  if (speed > 0) board.spiHz = (uint32_t)speed;
  board.panelInit = true;
  return true;
}
void Arduino_HWSPI::beginWrite() { busBytes = 0; }
void Arduino_HWSPI::endWrite() {
  // the transfer takes its time on the wire: this is what paces Bug Hop's frames
  spendUs(busBytes * 8ULL * 1000000ULL / board.spiHz);
  busBytes = 0;
}
void Arduino_HWSPI::writeCommand(uint8_t c) { busBytes++; panelCommand(c); }
void Arduino_HWSPI::writeCommand16(uint16_t c) { busBytes += 2; panelCommand((uint8_t)(c >> 8)); panelCommand((uint8_t)c); }
void Arduino_HWSPI::writeCommandBytes(uint8_t *data, uint32_t len) { for (uint32_t i = 0; i < len; i++) writeCommand(data[i]); }
void Arduino_HWSPI::write(uint8_t d) { busBytes++; panelData(d); }
void Arduino_HWSPI::write16(uint16_t d) { busBytes += 2; panelData((uint8_t)(d >> 8)); panelData((uint8_t)d); }
void Arduino_HWSPI::writeRepeat(uint16_t p, uint32_t len) { while (len--) write16(p); }
void Arduino_HWSPI::writeBytes(uint8_t *data, uint32_t len) { for (uint32_t i = 0; i < len; i++) write(data[i]); }
void Arduino_HWSPI::writePixels(uint16_t *data, uint32_t len) { for (uint32_t i = 0; i < len; i++) write16(data[i]); }

// What the user sees: the glass, turned the way the firmware's rotation says
// the Buddy is mounted. Black while the panel sleeps or the backlight is off.
void simPanelView(std::vector<uint16_t> &out, int &w, int &h) {
  const bool sideways = panelMadctl & 0x20;
  w = sideways ? RAM_H : GLASS_W;
  h = sideways ? GLASS_W : RAM_H;
  out.assign((size_t)w * h, 0);
  if (!board.panelOn || board.backlight <= 0) return;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int col = sideways ? y + GLASS_X : x + GLASS_X, row = sideways ? x : y;
      if (panelMadctl & 0x40) col = RAM_W - 1 - col;
      if (panelMadctl & 0x80) row = RAM_H - 1 - row;
      out[(size_t)y * w + x] = panelRam[row * RAM_W + col];
    }
  }
}

