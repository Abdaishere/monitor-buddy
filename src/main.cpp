// ============================================================
// Monitor-Buddy — Waveshare ESP32-C6-Touch-LCD-1.47
// by IdefixRC - https://github.com/IdefixRC
// Features: animated faces, clock, date, weather, moon,
//           stock ticker, GitHub stats.  
//           5 different themes available,
//           Enable/Disable pages to customize your Buddy
//           Touch swipe to change pages, tap the stock page
//           to see the next stock,
//           double tap to stop auto scrolling;
//           WifiManager creates a Wi-Fi hotspot with a
//           captive portal so you can set your Wi-Fi up;
//           tilt via QMI8658 IMU animates eyes.
//
// ------------------------------------------------------------
// ACKNOWLEDGEMENTS
// ------------------------------------------------------------
// Monitor Buddy uses ideas and code snippets from a few other creators
// Initial Baseline Idea: schematik.io - Tiny ESP DeskBuddy
// Additional Faces: https://github.com/EDISON-SCIENCE-CORNER/DESKBUDDY-1.0
//
// ------------------------------------------------------------
//  !! BEFORE YOU FLASH — READ THIS !!
// ------------------------------------------------------------
//  This is a PlatformIO project. Open the folder in VS Code with
//  the pioarduino IDE extension. The full step-by-step flashing
//  guide is in README.md, chapter 4. Short version:
//
//  1) Platform, board, partition table (huge_app.csv) and the
//     three libraries (Arduino_GFX, ArduinoJson v7, AyresWiFiManager
//     2.5.0) are all declared in platformio.ini and installed
//     automatically on the first build. Nothing to install by hand.
//     Note: AWM internally uses the older StaticJsonDocument /
//     DynamicJsonDocument names. Under ArduinoJson 7 those still
//     compile but emit *deprecation warnings*. Warnings only —
//     the build succeeds. Do not "fix" them by downgrading to v6,
//     that would break this sketch's JsonDocument usage.
//
//  2) There is no filesystem image to upload. The Wi-Fi setup
//     pages are built into AyresWiFiManager and follow the phone's
//     language (English, Spanish, German). Saved Wi-Fi and
//     brightness live on LittleFS and survive firmware updates.
//
// ------------------------------------------------------------
//  HOW TO CONFIGURE WI-FI
// ------------------------------------------------------------
//  Automatically: with no stored Wi-Fi the captive portal opens
//  by itself at boot. A stored network that fails is retried
//  every minute.
//
//  On demand: PRESS AND HOLD ANYWHERE ON THE TOUCH SCREEN for
//  ~2.5 s. Works from any page, at any time.
//
//  Then: join the Wi-Fi network  "Monitor-Buddy-Setup"
//        password                "buddy1234"
//        browse to               http://192.168.4.1
//        (most phones pop the portal open automatically)
//  Pick your network, enter the password, save. The board stores
//  the credentials in LittleFS (/wifi.json) and reboots.
//
// ------------------------------------------------------------
//  HOW TO USE
// ------------------------------------------------------------
// To customize Monitor-Buddy to you (GitHub user, timezone, stocks (up to
// five, set in the web flasher) + Finnhub API key, weather location,
// theme, which pages to show): copy  config/config.h.example  to  config/config.h  and edit
// the values there. config/config.h is gitignored so your API key
// never lands on GitHub.
//
// Use Monitor-Buddy:
// Setup Wifi on first boot by connecting to the Monitor-Buddy access point (AP)
// Press and hold the touch screen for 3 sec at any time later to redo the Wifi Setup (AP will start and you can configure your Monitor-Buddy
// Swipe left/right to move through the different pages
// Double Tap to disable and enable auto scrolling
// On the Face Page: single press the screen to switch to different faces
// Stock page: tap for the next stock
//
// Enjoy and have fun with Monitor-Buddy
//
// ============================================================

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <Update.h>
#include <Wire.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_app_desc.h>
#include <esp_ota_ops.h>
#include <esp_core_dump.h>
#include <math.h>
#include <string.h>

#include <LittleFS.h>
#include <SD.h>
#include <AnimatedGIF.h>
#include <sd_diskio.h>
#include <diskio.h>
#include <AyresWiFiManager.h>
#include <esp_mac.h>
#include "settings.h"
#include "nightmode.h"
#include "protocol.h"
#include "display_fmt.h"
#include "button.h"
#include "coin.h"
#include "github_scan.h"
#include "gh_avatar.h"
#include "odoo_fmt.h"
#include "keys.h"
#include "persona.h"
#include "lunch_face.h"
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

// Set from the release tag by the release workflow (-DFW_VERSION=\"1.2.0\").
#ifndef FW_VERSION
  #define FW_VERSION "dev"
#endif

// USER CONFIGURATION --------> EDIT config/config.h
// Copy config/config.h.example to
// config/config.h and edit it there — that file is gitignored so
// your Finnhub API key never gets committed. If config/config.h is
// absent the build falls back to config/config.h.example, whose empty
// Finnhub key and GitHub user hide the stock and GitHub pages.
#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.h.example"
#endif

// Mounting location (config.h): LEFT or RIGHT. The word is turned into a
// number by token pasting (RIGHT -> MOUNT_ID_RIGHT), so LEFT and RIGHT are
// never defined as macros of their own and can't clash with library names.
// Older config.h copies predate the setting, so default to LEFT.
#ifndef MOUNTING_LOCATION
  #define MOUNTING_LOCATION LEFT
#endif
#define MOUNT_ID_LEFT    1
#define MOUNT_ID_RIGHT   2
#define MOUNT_CAT_(a, b) a##b
#define MOUNT_CAT(a, b)  MOUNT_CAT_(a, b)
#define MOUNT_ID         MOUNT_CAT(MOUNT_ID_, MOUNTING_LOCATION)
#if MOUNT_ID != MOUNT_ID_LEFT && MOUNT_ID != MOUNT_ID_RIGHT
  #error "MOUNTING_LOCATION in config/config.h must be LEFT or RIGHT"
#endif
#define MOUNT_IS_RIGHT   (MOUNT_ID == MOUNT_ID_RIGHT)

// Optional POSIX time zone rule with daylight saving, e.g.
// "CET-1CEST,M3.5.0,M10.5.0/3". Older config.h copies don't have it; empty
// means "build a fixed rule from TZ_OFFSET_HOURS".
#ifndef TZ_RULE
  #define TZ_RULE ""
#endif

// Odoo page (config.h). Its data comes from the PC: odooFromPc().
#ifndef ODOO_TRIGRAM
  #define ODOO_TRIGRAM "ODOO"
#endif
#ifndef ODOO_DAY_HOURS
  #define ODOO_DAY_HOURS 8
#endif

// The animations that play by themselves (§7.7 of docs/HANDOVER.md), file names
// in the card's gifs folder. All three are drawn by tools/buddy_art.py; set
// another name in config.h to play a GIF of your own instead.
#ifndef GIF_FULL_DAY
  #define GIF_FULL_DAY "full-day.gif"       // the Odoo hours pass ODOO_DAY_HOURS
#endif
#ifndef GIF_CI_GREEN
  #define GIF_CI_GREEN "ci-green.gif"       // the last failing pull request passes
#endif
#ifndef GIF_FOCUS_DONE
  #define GIF_FOCUS_DONE "focus-done.gif"   // a focus session of 25 minutes or more ends
#endif

// The name on the bounce screensaver (setting "name"); empty shows BUDDY.
#ifndef YOUR_NAME
  #define YOUR_NAME ""
#endif

// Wi-Fi link: the key tools/buddy_link.py shares with the Buddy, 64 hex
// digits. Normally it arrives over USB ("@MB KEYS", see loadKeys()); a value
// here in config.h is only the fallback of a board that was given none. With
// neither, the link stays closed.
#ifndef LINK_KEY
  #define LINK_KEY ""
#endif
// PEM public key of the key tools/buddy_flash.py signs updates with; it comes
// the same way. Without it no update is taken over Wi-Fi.
#ifndef UPDATE_KEY
  #define UPDATE_KEY ""
#endif

// OTHER CONFIGURATION ----> EDIT ONLY IF NECESSARY

// Access-point shown when the portal is open. Both can be set in config.h.
// AP_PASS: 8-26 characters (WPA2 needs at least 8, and the setup screen shows
// up to 26), or "" only if you want an open AP. AP_SSID: 1-26 characters.
#ifndef AP_SSID
  #define AP_SSID   "Monitor-Buddy-Setup"
#endif
#ifndef AP_PASS
  #define AP_PASS   "buddy1234"
#endif
static_assert(sizeof(AP_PASS) == 1 || (sizeof(AP_PASS) >= 9 && sizeof(AP_PASS) <= 27),
              "AP_PASS must be empty or 8-26 characters");
static_assert(sizeof(AP_SSID) >= 2 && sizeof(AP_SSID) <= 27, "AP_SSID must be 1-26 characters");
#define AWM_HOSTNAME "Monitor-Buddy"

// ── Personal-build options (config.h) ───────────────────────
// The defaults for what the web flasher can change on the board.
// CLOCK_12H       true shows 1-12 with AM/PM instead of 00-23.
// NIGHT_MODE      true dims the backlight and gives the face a sleepy look
//                 from NIGHT_START_HOUR until NIGHT_END_HOUR (local time).
//                 A tap brings the normal brightness back for
//                 NIGHT_WAKE_MS. Off until the clock has synced over NTP.
// NIGHT_BRIGHTNESS backlight level at night, 8-255.
#ifndef CLOCK_12H
  #define CLOCK_12H false
#endif
#ifndef NIGHT_MODE
  #define NIGHT_MODE false
#endif
#ifndef NIGHT_START_HOUR
  #define NIGHT_START_HOUR 23
#endif
#ifndef NIGHT_END_HOUR
  #define NIGHT_END_HOUR 7
#endif
#ifndef NIGHT_BRIGHTNESS
  #define NIGHT_BRIGHTNESS 26
#endif
#ifndef NIGHT_WAKE_MS
  #define NIGHT_WAKE_MS 30000UL
#endif
static_assert(NIGHT_START_HOUR >= 0 && NIGHT_START_HOUR <= 23, "NIGHT_START_HOUR must be 0-23");
static_assert(NIGHT_END_HOUR >= 0 && NIGHT_END_HOUR <= 23, "NIGHT_END_HOUR must be 0-23");
static_assert(NIGHT_BRIGHTNESS >= 8 && NIGHT_BRIGHTNESS <= 255, "NIGHT_BRIGHTNESS must be 8-255");
static_assert(NIGHT_START_HOUR != NIGHT_END_HOUR, "NIGHT_START_HOUR and NIGHT_END_HOUR must differ");

// How long the portal stays open with nobody using it, in seconds.
// The timer is reset by any connected AP client and by every HTTP
// request, so it will not close while you are actually using it.
#define PORTAL_TIMEOUT_S 300

// ── AWM GPIO assignment — DO NOT USE THE LIBRARY DEFAULTS ──
// AyresWiFiManager's constructor defaults are ledPin = 2 and
// buttonPin = 0. On this board GPIO2 is LCD_MOSI and GPIO1 is
// LCD_SCK: AWM's begin() calls pinMode(ledPin, OUTPUT) and
// digitalWrite(ledPin, LOW), which would clamp the SPI data line
// and blank the display. The pins below are passed explicitly to
// the constructor to avoid that.
//
// Pins already taken on the ESP32-C6-Touch-LCD-1.47:
//   1, 2, 14, 15, 22, 23  → LCD SPI / DC / CS / RST / backlight
//   3, 4                  → TF card data-in / CS (clock and data-out are the LCD's)
//   18, 19, 20, 21        → touch I2C / RST / INT
//   5, 6                  → QMI8658 IMU interrupts
//   8                     → boot-mode strapping + onboard RGB LED
//   9                     → BOOT button
//   12, 13                → USB-Serial-JTAG
// GPIO7 is free, broken out, and is not a strapping pin, so it is
// used as a harmless "status LED" sink. Nothing is wired to it —
// AWM just toggles it. If you solder an LED to another free pin,
// change AWM_LED_PIN to match.
#define AWM_LED_PIN     7
// AWM's button is that idle pin too, which reads high: a BOOT press during a
// restart (a hand on the Buddy, a card going in) erased the stored Wi-Fi.
#define AWM_BUTTON_PIN  7

// Wi-Fi retry behaviour once we are running.
// AWM runs the reconnect in the background, so neither value blocks the UI.
#define WIFI_RETRY_INTERVAL_MS  60000UL  // wait after a failed attempt
#define WIFI_RETRY_WINDOW_MS    15000UL  // how long one attempt may take to connect

// Hold the touchscreen this long to open the portal on demand.
#define TOUCH_PORTAL_HOLD_MS     2500UL

// Stock Market Refresh Settings
// !! API QUOTA HEADS-UP !!  Finnhub's free tier allows 60 API calls per minute.
// One call per minute is well within that, so this configuration will not hit
// the rate limit. Fetches only happen while the stock page is actually shown.
#define STOCK_REFRESH_MS  60000UL   // minimum ms between stock API calls (60 s)
// The coin on screen: its price every minute, its hourly chart every 15 minutes.
static constexpr uint32_t COIN_REFRESH_MS       = 60000UL;
static constexpr uint32_t COIN_CHART_REFRESH_MS = 15UL * 60UL * 1000UL;

static constexpr uint8_t BRIGHTNESS_DEFAULT = 180;
static constexpr uint8_t BRIGHTNESS_MINIMUM = 8;
// Save brightness this long after the last step, so a drag writes once.
static constexpr uint32_t BRIGHTNESS_SAVE_DELAY_MS = 2000;

// Fetch timeouts. Fetches run in netTask, so these bound how long one stuck
// fetch can hold up the next, not the UI. Worst case per fetch is about 20 s.
static constexpr uint32_t HTTP_TIMEOUT_MS         = 5000UL;  // response wait
static constexpr int32_t  HTTP_CONNECT_TIMEOUT_MS = 4000;    // TCP connect (lib default 5 s)
static constexpr uint32_t TLS_HANDSHAKE_TIMEOUT_S = 8;       // TLS (lib default 120 s)
static constexpr uint32_t NTP_RETRY_INTERVAL_MS        = 60000UL;
// After a failed fetch, retry after FETCH_RETRY_MIN_MS, doubling each further
// failure up to FETCH_RETRY_MS (GitHub: GITHUB_RETRY_MAX_MS, to stay well under
// its 60 calls/hour unauthenticated limit). A success resets the backoff.
static constexpr uint32_t FETCH_RETRY_MIN_MS           = 10000UL;
static constexpr uint32_t FETCH_RETRY_MS               = 60000UL;
static constexpr uint32_t GITHUB_RETRY_MAX_MS          = 120000UL;
static constexpr uint32_t WEATHER_REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
static constexpr uint32_t GITHUB_REFRESH_INTERVAL_MS  = 30UL * 60UL * 1000UL;
static constexpr uint32_t ODOO_STALE_MS = 30UL * 60UL * 1000UL;   // Odoo data from the PC older than this is dropped

// Background fetch task. With certificate checks off, an HTTPS fetch used
// about 3.3 KB; verifying the server's chain takes it to about 3.8 KB
// (measured: 8.4 KB unused of 12 KB). 12 KB keeps a wide margin.
// If "[net] ... stack free" ever drops below 2048 bytes, raise this.
static constexpr uint32_t    NET_TASK_STACK_BYTES = 12288;
static constexpr UBaseType_t NET_TASK_PRIORITY    = 1;     // same as loop()
static constexpr uint32_t    NET_HEALTH_LOG_MS    = 5UL * 60UL * 1000UL;

// ── Pin definitions ────────────────────────────────────────
#define LCD_BL    23
#define LCD_DC    15
#define LCD_CS    14
#define LCD_SCK    1
#define LCD_MOSI   2
#define LCD_RST   22

#define TOUCH_SDA 18
#define TOUCH_SCL 19
#define TOUCH_RST 20
#define TOUCH_INT 21
// The TF card shares the display's SPI clock and data-out lines.
#define SD_MISO    3
#define SD_CS      4
// BOOT, pressed = low: asks the PC to lock (readBootButton()).
#define BOOT_PIN   9


struct AccelData {
  float accelX;
  float accelY;
  float accelZ;
  uint32_t timestamp;
};

struct GyroData {
  float gyroX;
  float gyroY;
  float gyroZ;
  uint32_t timestamp;
};

struct calData {
  bool valid;
  float accelBias[3];
  float gyroBias[3];
};

struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

struct touch_data_t {
  uint8_t    count;
  TouchPoint coords[1];  // first active touch point used by this UI
};


// Forward declarations
static uint8_t currentClockHour();

void triggerDoubleTap();

void resetSharedI2CBus();
void bsp_touch_init(TwoWire *wire, uint8_t rstPin, uint8_t intPin, uint8_t rotation, uint16_t dispW, uint16_t dispH);
void bsp_touch_read();
uint16_t clampTouchCoord(int32_t value, uint16_t maxValue);
uint16_t scaleTouchAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, uint16_t outMax);
bool bsp_touch_get_coordinates(uint16_t *outX, uint16_t *outY);
uint16_t rgb(uint8_t r, uint8_t g, uint8_t b);
float clampFloat(float v, float lo, float hi);
void lcdRegInit();
uint32_t compileTimeSeconds();
uint8_t compileMonthNumber();
int32_t daysFromCivil(int32_t y, uint8_t mo, uint8_t d);
void civilFromDays(int32_t z, int32_t *year, uint8_t *month, uint8_t *day);
int32_t compileDateDays();
void seedClockFromCompileTime();
void centeredText(const char *text, int y, uint8_t size);
void drawPageDots();
void drawHeader(const char *title);
void switchApp(int8_t delta, bool swiped = false);
void buildPageList();
void buildStockList();
void buildCoinList();
bool coinConfigured();
bool fetchCoin();
void drawCoin();
bool wifiConfigured();
bool githubConfigured();
bool stockConfigured();
bool ensureWifi();
void syncNTP();
void drawWeatherIcon(int cx, int cy, int code, bool isDay, uint16_t color);
bool fetchWeather();
bool fetchStock();
bool fetchGithub();
void drawFace(float tx, float ty);
void drawDigitSegment(int x, int y, int w, int h, int t, uint8_t seg, uint16_t color);
void drawDigit(int x, int y, uint8_t digit, int w, int h, int t, uint16_t color);
void drawClock();
void drawDatePage();
void drawWeather();
void drawMoonDisc(int cx, int cy, int radius, float phase, uint16_t color);
void drawMoon();
void drawStock();
void drawGithub();
void drawPrs();
void triggerFaceTap();
void readSensors();
void readTouch();
void updateFaceTimers();
void updateBuddyMood();
void updateAutoPage();
void calibrateNeutral();
void loadDisplaySettings();
Settings defaultsFromConfig();
void loadSettings();
void loadKeys();
void serviceProtocol();
void saveDisplaySettings();
void setBrightness(int level);
void flushBrightnessSave(bool now);
void writeBacklight(uint8_t level);
uint8_t backlightTarget();
bool nightDimming();
void nightWake();
void updateNightMode();
void setNightBrightness(uint8_t level);
void flushNightSave(bool now);

// V2 additions
void drawBootMessage(const char *l1, const char *l2, const char *l3);
void drawPortalScreen();
void servicePortal();
void maintainWifi();
void openSetupPortal(const char *reason);

uint32_t lastI2CResetMs = 0;

void resetSharedI2CBus() {
  uint32_t now = millis();
  if (now - lastI2CResetMs < 250) return;
  lastI2CResetMs = now;
  Wire.end();
  delay(5);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);   // the panel and the motion sensor both take fast mode
}

#define IMU_ADDRESS 0x6B
// The sensor's values as knockTask() last read them; the loop copies them under this.
portMUX_TYPE imuMux = portMUX_INITIALIZER_UNLOCKED;
// The I2C bus of the touch panel and the sensor: knockTask() and the loop take turns.
SemaphoreHandle_t i2cMutex = nullptr;

class QMI8658Mini {
 public:
  int init(calData cal, uint8_t address = IMU_ADDRESS) {
    imuAddress = address;
    calibration = cal;
    if (read8(0x00) != 0x05) return -1;  // WHO_AM_I
    write8(0x60, 0xFF);                  // soft reset
    delay(100);
    write8(0x02, 0x40);                  // CTRL1: auto-increment
    setAccelRange(4);
    setGyroRange(512);
    write8(0x06, 0x03);                  // CTRL5: accel/gyro low-pass defaults
    write8(0x08, 0x03);                  // CTRL7: enable accel + gyro
    delay(100);
    return 0;
  }

  int setAccelRange(int range) {
    uint8_t config = 0x10;
    if (range == 2) { accelScale = 2.0f / 32768.0f; config = 0x00; }
    else if (range == 4) { accelScale = 4.0f / 32768.0f; config = 0x10; }
    else if (range == 8) { accelScale = 8.0f / 32768.0f; config = 0x20; }
    else if (range == 16) { accelScale = 16.0f / 32768.0f; config = 0x30; }
    else return -1;
    write8(0x08, 0x00);
    rmw8(0x03, 0x70, config);            // CTRL2 accel range bits
    write8(0x08, 0x03);
    return 0;
  }

  int setGyroRange(int range) {
    uint8_t config = 0x50;
    if (range == 128 || range == 125) { gyroScale = 128.0f / 32768.0f; config = 0x30; }
    else if (range == 256 || range == 250) { gyroScale = 256.0f / 32768.0f; config = 0x40; }
    else if (range == 512 || range == 500) { gyroScale = 512.0f / 32768.0f; config = 0x50; }
    else if (range == 1024 || range == 1000) { gyroScale = 1024.0f / 32768.0f; config = 0x60; }
    else if (range == 2048 || range == 2000) { gyroScale = 2048.0f / 32768.0f; config = 0x70; }
    else return -1;
    write8(0x08, 0x00);
    rmw8(0x04, 0x70, config);            // CTRL3 gyro range bits
    write8(0x08, 0x03);
    return 0;
  }

  void update() {
    uint8_t status = read8(0x2E);        // STATUS0: accel/gyro ready bits
    if ((status & 0x03) == 0) return;
    uint8_t raw[12] = {0};
    if (!readBytes(0x35, raw, sizeof(raw))) return;

    int16_t ax = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ay = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t az = (int16_t)((raw[5] << 8) | raw[4]);
    int16_t gx = (int16_t)((raw[7] << 8) | raw[6]);
    int16_t gy = (int16_t)((raw[9] << 8) | raw[8]);
    int16_t gz = (int16_t)((raw[11] << 8) | raw[10]);
    uint32_t now = micros();

    taskENTER_CRITICAL(&imuMux);
    accel.accelX = ax * accelScale - calibration.accelBias[0];
    accel.accelY = ay * accelScale - calibration.accelBias[1];
    accel.accelZ = az * accelScale - calibration.accelBias[2];
    accel.timestamp = now;
    gyro.gyroX = gx * gyroScale - calibration.gyroBias[0];
    gyro.gyroY = gy * gyroScale - calibration.gyroBias[1];
    gyro.gyroZ = gz * gyroScale - calibration.gyroBias[2];
    gyro.timestamp = now;
    taskEXIT_CRITICAL(&imuMux);
  }
  // The pull in g on each axis, acceleration only: what the knock watch reads a few hundred times a second.
  bool pull(float axes[3]) {
    uint8_t raw[6];
    if (!readBytes(0x35, raw, sizeof(raw))) return false;
    for (int i = 0; i < 3; i++) axes[i] = (int16_t)(raw[2 * i + 1] << 8 | raw[2 * i]) * accelScale;
    return true;
  }

  void getAccel(AccelData *out) { taskENTER_CRITICAL(&imuMux); *out = accel; taskEXIT_CRITICAL(&imuMux); }
  void getGyro(GyroData *out) { taskENTER_CRITICAL(&imuMux); *out = gyro; taskEXIT_CRITICAL(&imuMux); }

 private:
  uint8_t imuAddress = IMU_ADDRESS;
  float accelScale = 4.0f / 32768.0f;
  float gyroScale = 512.0f / 32768.0f;
  calData calibration = {0};
  AccelData accel = {0};
  GyroData gyro = {0};

  uint8_t read8(uint8_t reg) {
    uint8_t value = 0;
    readBytes(reg, &value, 1);
    return value;
  }

  bool readBytes(uint8_t reg, uint8_t *buffer, uint8_t len) {
    Wire.beginTransmission(imuAddress);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;   // a repeated start: one exchange
    if (Wire.requestFrom((uint8_t)imuAddress, len, (uint8_t)true) != len) {
      return false;
    }
    for (uint8_t i = 0; i < len; i++) buffer[i] = Wire.read();
    return true;
  }

  void write8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(imuAddress);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }

  void rmw8(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t current = read8(reg);
    write8(reg, (current & ~mask) | (value & mask));
  }
};

// ── AXS5106L inline touch reader ──────────────────────────
// The AXS5106L is the capacitive touch controller on the
// Waveshare ESP32-C6-Touch-LCD-1.47. ESP-IDF components exist,
// but this Arduino starter inlines a small polling reader so it
// does not need the ESP-IDF/LVGL touch stack.
// Protocol: I2C @ 400 kHz, 7-bit device address 0x63.
// Touch packets are read from register 0x01. The packet starts
// with gesture_id, touch_count, then point data. This sketch uses
// the first active point for tap/swipe navigation.

#define AXS5106L_ADDR 0x63
#define AXS5106L_TOUCH_DATA_REG 0x01

static TwoWire *_touchWire = nullptr;
static uint8_t  _touchRst  = 255;
static uint8_t  _touchInt  = 255;
static uint16_t _touchW    = 320;
static uint16_t _touchH    = 172;
static uint8_t  _touchRot  = 0;

void bsp_touch_init(TwoWire *wire, uint8_t rstPin, uint8_t intPin,
                    uint8_t rotation, uint16_t dispW, uint16_t dispH) {
  _touchWire = wire;
  _touchRst  = rstPin;
  _touchInt  = intPin;
  _touchRot  = rotation;
  _touchW    = dispW;
  _touchH    = dispH;

  if (_touchRst != 255) {
    pinMode(_touchRst, OUTPUT);
    digitalWrite(_touchRst, LOW);
    delay(200);
    digitalWrite(_touchRst, HIGH);
    delay(300);
  }
  if (_touchInt != 255) {
    pinMode(_touchInt, INPUT_PULLUP);
  }
}

// bsp_touch_read — no-op for polling mode; INT pin can be
// checked externally if needed.
void bsp_touch_read() {}

uint16_t clampTouchCoord(int32_t value, uint16_t maxValue) {
  if (value < 0) return 0;
  if (value >= maxValue) return maxValue - 1;
  return (uint16_t)value;
}

uint16_t scaleTouchAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, uint16_t outMax) {
  if (rawMax <= rawMin || outMax == 0) return 0;
  if (raw <= rawMin) return 0;
  if (raw >= rawMax) return outMax - 1;
  return (uint32_t)(raw - rawMin) * (outMax - 1) / (rawMax - rawMin);
}

// Returns true if at least one touch point is active.
static bool touchCoordinates(uint16_t *outX, uint16_t *outY);

// The panel and the motion sensor share the I2C bus: knockTask() and the loop take turns.
bool bsp_touch_get_coordinates(uint16_t *outX, uint16_t *outY) {
  if (i2cMutex) xSemaphoreTake(i2cMutex, portMAX_DELAY);
  const bool touched = touchCoordinates(outX, outY);
  if (i2cMutex) xSemaphoreGive(i2cMutex);
  return touched;
}

static bool touchCoordinates(uint16_t *outX, uint16_t *outY) {
  if (!_touchWire || !outX || !outY) return false;

  // Read the complete AXS5106L frame. Its second byte is a full-byte touch
  // count; reading the complete frame also keeps the controller's read pointer
  // aligned for the next poll.
  _touchWire->beginTransmission(AXS5106L_ADDR);
  _touchWire->write(AXS5106L_TOUCH_DATA_REG);
  if (_touchWire->endTransmission(true) != 0) {
    resetSharedI2CBus();
    return false;
  }
  delayMicroseconds(300);

  uint8_t len = _touchWire->requestFrom((uint8_t)AXS5106L_ADDR, (uint8_t)14, (uint8_t)true);
  if (len < 6) {
    resetSharedI2CBus();
    return false;
  }

  uint8_t buf[14];
  for (uint8_t i = 0; i < len && i < sizeof(buf); i++) buf[i] = _touchWire->read();

  uint8_t nPoints = buf[1];
  if (nPoints == 0 || nPoints > 2) return false;

  // First point begins at byte 2: x_hi/event, x_lo, y_hi/id, y_lo.
  uint16_t rawX = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];
  uint16_t rawY = ((uint16_t)(buf[4] & 0x0F) << 8) | buf[5];
  if ((rawX == 0x0FFF && rawY == 0x0FFF) || rawX > 4090 || rawY > 4090) return false;

  // Small edge dead-zone compensation. The controller reports raw axes with a
  // few pixels of slack at the extremes; scaling them to the active screen area
  // makes edge swipes less sticky while preserving the current orientation.
  const uint16_t edge = 3;
  uint16_t mappedX = rawX;
  uint16_t mappedY = rawY;
  switch (_touchRot) {
    case 1:  // landscape, default for this board
      mappedX = scaleTouchAxis(rawY, edge, _touchW > edge ? _touchW - 1 - edge : _touchW - 1, _touchW);
      mappedY = scaleTouchAxis(rawX, edge, _touchH > edge ? _touchH - 1 - edge : _touchH - 1, _touchH);
      break;
    case 2:
      mappedX = _touchW - 1 - scaleTouchAxis(rawX, edge, _touchW > edge ? _touchW - 1 - edge : _touchW - 1, _touchW);
      mappedY = _touchH - 1 - scaleTouchAxis(rawY, edge, _touchH > edge ? _touchH - 1 - edge : _touchH - 1, _touchH);
      break;
    case 3:  // landscape turned 180° (right-side mount): case 1 with both axes flipped
      mappedX = _touchW - 1 - scaleTouchAxis(rawY, edge, _touchW > edge ? _touchW - 1 - edge : _touchW - 1, _touchW);
      mappedY = _touchH - 1 - scaleTouchAxis(rawX, edge, _touchH > edge ? _touchH - 1 - edge : _touchH - 1, _touchH);
      break;
    default:  // 0 — portrait
      mappedX = scaleTouchAxis(rawX, edge, _touchW > edge ? _touchW - 1 - edge : _touchW - 1, _touchW);
      mappedY = scaleTouchAxis(rawY, edge, _touchH > edge ? _touchH - 1 - edge : _touchH - 1, _touchH);
      break;
  }
  *outX = clampTouchCoord(mappedX, _touchW);
  *outY = clampTouchCoord(mappedY, _touchH);
  return true;
}
// ── End AXS5106L driver ────────────────────────────────────

#ifdef MBSIM_SCREEN_W   // simulator only: tools/sim `make SCREEN=240x280` tries the UI on another panel
static const int SCREEN_W         = MBSIM_SCREEN_W;
static const int SCREEN_H         = MBSIM_SCREEN_H;
#else
static const int SCREEN_W         = 320;
static const int SCREEN_H         = 172;
#endif
static const uint8_t APP_COUNT    = 8;   // total number of pages defined

// Page identifiers, in fixed display order. These index the render switch in
// loop(); which are actually shown is decided by settings.pages.
static constexpr uint8_t PAGE_FACE    = 0;
static constexpr uint8_t PAGE_CLOCK   = 1;
static constexpr uint8_t PAGE_DATE    = 2;
static constexpr uint8_t PAGE_WEATHER = 3;
static constexpr uint8_t PAGE_MOON    = 4;
static constexpr uint8_t PAGE_STOCK   = 5;
static constexpr uint8_t PAGE_GITHUB  = 6;
static constexpr uint8_t PAGE_COIN    = 7;
// The AbdA page has no switch in settings.pages: it is always shown, last.
static constexpr uint8_t PAGE_ABDA    = APP_COUNT;
// The Odoo page has no switch either: it shows once the PC has sent Odoo data.
static constexpr uint8_t PAGE_ODOO    = APP_COUNT + 1;
// My open pull requests, as the PC reports them. Skipped while there are none.
static constexpr uint8_t PAGE_PRS     = APP_COUNT + 2;
// The animations in /gifs, one per visit. Skipped while there are none.
static constexpr uint8_t PAGE_GIFS    = APP_COUNT + 3;
// The day at a glance: the next meeting, the hours, pull requests and Odoo.
// Reached by swipe, or shown by itself (todayShow()).
static constexpr uint8_t PAGE_TODAY   = APP_COUNT + 4;
// Bug Hop, a one-tap game.
static constexpr uint8_t PAGE_GAME    = APP_COUNT + 5;
// Claude's limits and the open Claude Code sessions.
static constexpr uint8_t PAGE_CLAUDE  = APP_COUNT + 6;
// The working day before, ahead of the day's first Today page. Not in the round.
static constexpr uint8_t PAGE_YESTERDAY = APP_COUNT + 7;

// -- Face expressions (ported from DESKBUDDY-1.0, Edison Science Corner) --
// 9 moods, cycled by single-tapping the face page. A fast spin of the board
// still snaps to SURPRISED (see readSensors).
#define MOOD_NORMAL      0
#define MOOD_HAPPY       1
#define MOOD_SURPRISED   2
#define MOOD_SLEEPY      3
#define MOOD_ANGRY       4
#define MOOD_SAD         5
#define MOOD_EXCITED     6
#define MOOD_LOVE        7
#define MOOD_SUSPICIOUS  8
static const uint8_t FACE_MOOD_COUNT = 9;
// Status faces. Set by updateBuddyMood() only: they are past FACE_MOOD_COUNT,
// so a tap never cycles to them.
#define MOOD_FOCUS       9    // narrowed frown, pupils reading line by line, teal glint and cursor
#define MOOD_LUNCH       10   // the chewing picture in lunch_face.h
#define MOOD_AWAY        11   // eyes shut to a breathing line, two z drifting up
#define MOOD_NUDGE       12   // sad trembling eyes with a falling tear
#define MOOD_CI          13   // red frown that shakes, a flashing "!"
#define MOOD_ONAIR       14   // a studio sign in place of the eyes
#define MOOD_DIZZY       15   // spirals for pupils, the head sways, stars circle overhead
#define MOOD_UNIMPRESSED 16   // half-shut flat lids and a stare straight ahead
#define MOOD_PARTY       17   // happy eyes under a party hat, confetti falling
static const uint8_t MOOD_TOTAL = 17;
static const uint32_t PAGE_AUTO_INTERVAL_MS = 8000;
static const uint32_t FRAME_MS = 33;   // 30 frames a second at most
static const uint16_t FG = RGB565_WHITE;
static const uint16_t BG = RGB565_BLACK;
// Landscape, turned 180° for a right-side mount (settings.mount, applied
// in setup()). Drives the display and the touch mapping; tiltSign flips the
// IMU tilt to match.
uint8_t screenRotation = 1;
float   tiltSign       = 1.0f;

// ── Settings ──────────────────────────────────────────────
// Loaded once in setup() from /config.json, with the config.h values as the
// defaults. Only night.brightness changes while running, written from the
// loop by a night swipe, and netTask never reads it. The rest stays as loaded:
// every other save restarts the board, so netTask may read them without a lock.
// settings.brightness is only a copy of brightnessLevel for GET and for the
// file: /display.json is the one truth, so a swipe never creates /config.json
// and the copy in the file is ignored at boot. Refresh it before using it.
static const char *const CONFIG_PATH = "/config.json";
Settings  settings;
AddedKeys settingsAdded;   // keys /config.json lacked; reported by GET
bool settingsStored = false;   // /config.json was readable at boot, or a self-save wrote it; GET "stored"

static_assert(PAGE_FACE == 0 && PAGE_CLOCK == 1 && PAGE_DATE == 2 &&
              PAGE_WEATHER == 3 && PAGE_MOON == 4 && PAGE_STOCK == 5 &&
              PAGE_GITHUB == 6 && PAGE_COIN == 7, "settings.pages[] is indexed by PAGE_* ids");
static_assert(SETTINGS_PAGE_COUNT == APP_COUNT,
              "settings.pages[] needs one entry per page");

Arduino_DataBus *bus     = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, SD_MISO);
#ifdef MBSIM_SCREEN_W
Arduino_GFX    *display  = new Arduino_ST7789(bus, LCD_RST, 0, false, SCREEN_H, SCREEN_W, 0, 0, 0, 0);
#else
Arduino_GFX    *display  = new Arduino_ST7789(bus, LCD_RST, 0, false, 172, 320, 34, 0, 34, 0);
#endif
Arduino_Canvas *gfx      = new Arduino_Canvas(SCREEN_W, SCREEN_H, display);

// ── Wi-Fi manager instance ────────────────────────────────
// Explicit pins — see the AWM GPIO note near the top. Never let
// this default-construct, or it will grab GPIO2 (LCD_MOSI).
AyresWiFiManager wifiManager(AWM_LED_PIN, AWM_BUTTON_PIN);

QMI8658Mini imu;
calData calib = {0};
AccelData accel;
GyroData gyro;

bool     imuReady        = false;
bool     touchReady      = false;
bool     autoPageEnabled  = true;
bool     touchWasDown    = false;
bool     ntpSynced       = false;
bool     clockTrusted    = false;   // set by the first NTP sync and never cleared:
                                    // the system clock stays right through the 6 h resyncs
bool     touchPortalArmed = false;  // one-shot latch for touch-and-hold
bool     portalNeedsRelease = false; // ignore the finger that opened the portal
volatile uint8_t currentApp = 0;  // page shown (PAGE_*); also read by netTask
uint8_t  pageOrder[APP_COUNT + 7];  // enabled page IDs, built from settings.pages at boot
uint8_t  pageCount       = 0;   // how many pages are enabled
uint8_t  currentPageIdx  = 0;   // index into pageOrder[] of the current page
uint8_t  faceMood        = 0;
bool faceWave = false;                // a hand waves beside the eyes (the hello at the end of setup)
uint32_t waveUntil = 0;
const char *faceCaption  = nullptr;   // what the Buddy is up to, under the eyes (updateBuddyMood)
uint8_t  faceStatus      = 0;         // the day's status on the face, 0 in demo mode (updateBuddyMood)
bool flushForced = false;   // the screen shows something else than the last flushChanged(): send it all
// The PC, as tools/buddy_link.py last reported it (STATE, over the Wi-Fi link
// or serial). claude: what a Claude Code session on the PC wants, project: its
// folder. prList: the PRs updated last, check: PR_CHECK_*, review: PR_REVIEW_*.
// mic: a program on the PC records from the microphone. reviews: pull requests
// that wait for my review, changes: mine with changes requested. use5h, use7d:
// Claude's usage limits in percent, when useKnown; useReset: when each resets,
// as the status line writes it ("19:10", "Sat"). merged, reviewed: pull
// requests merged and reviewed on the working day before. sessions: the open
// Claude Code sessions, the last changed first; state 0 working, else
// CLAUDE_NEEDS_YOU or CLAUDE_DONE, since that time in UTC seconds. in, out:
// today's first unlock and last lock as HH:MM, "" before there is one; yin,
// yout: the same of the work day before.
enum : uint8_t { CLAUDE_QUIET = 0, CLAUDE_NEEDS_YOU = 1, CLAUDE_DONE = 2 };
enum : uint8_t { PR_CHECK_NONE = 0, PR_CHECK_OK = 1, PR_CHECK_FAILED = 2, PR_CHECK_RUNNING = 3 };
enum : uint8_t { PR_REVIEW_NONE = 0, PR_REVIEW_CHANGES = 1, PR_REVIEW_APPROVED = 2 };
static const uint8_t PR_LIST_MAX = 4;
static const uint8_t PC_SESSION_MAX = 4;
struct PcPr { char title[52]; uint8_t check, review; };
struct PcSession { char project[24]; uint8_t state; uint32_t since; };
struct PcState {
  bool known, locked; int prs, prsFailing; uint8_t claude;
  uint8_t demo;   // show status N whatever the day says (100 = none), for screenshots
  PcPr prList[PR_LIST_MAX]; uint8_t prCount; uint32_t at;
  bool mic, useKnown; int reviews, changes; uint8_t use5h, use7d; char project[24];
  char useReset[2][8]; int merged, reviewed;
  PcSession sessions[PC_SESSION_MAX]; uint8_t sessionCount;
  char in[6], out[6], yin[6], yout[6];
};
PcState pc = {};
bool privacyLock = false;   // the PC said it is locked: the face only, until it says unlocked
// The timer counts up. Lay the Buddy flat for 2 s to start it, stand it up to
// stop it. Locking the PC stops it too.
float    filteredAz     = 0.0f;
uint32_t timerStartMs   = 0;       // 0 = not running
bool     timerByPc      = false;   // started by the PC: standing the Buddy up does not stop it
uint32_t timerDoneUntil = 0;       // the result stays on the face until then
uint32_t timerLastS     = 0;       // length of the session that ended last
// What the Buddy counted today, kept in DAY_PATH across restarts. best is the
// game's best score, which a new day keeps.
struct DayTotals { int32_t date; uint32_t focusS, meetS; uint16_t sessions, best; bool briefed; };
DayTotals day = {};
uint32_t micSinceMs     = 0;       // when the microphone went on; 0 = off
uint32_t shakeAtMs      = 0;       // the last jolt of a shake that is being answered; 0 = none
uint32_t knockAtMs      = 0;       // a double knock that is being greeted; 0 = none
const char *knockHello  = "HELLO!";
uint32_t todayUntil     = 0;       // the Today page is held on screen until then
const char *todayTitle  = "TODAY";
uint32_t yesterdayUntil = 0;       // within todayUntil: the Yesterday page comes first, until then
static constexpr uint32_t YESTERDAY_MS = 15000UL;
// A tap that joins a meeting and a BOOT press that locks the PC, until the
// answer to buddy_link.py's next P or STATE carries them (linkAnswer()).
uint32_t joinAskedAt    = 0;
uint32_t lockAskedAt    = 0;
static constexpr uint32_t LINK_ASK_MS = 10000UL;    // a request still waiting then is dropped
uint32_t joinShownUntil = 0;       // the face says OPENING THE LINK until then
static constexpr uint32_t JOIN_SHOW_MS = 4000UL;
char     noticeLines[4][27];       // a line sent with buddy-say, wrapped
uint8_t  noticeCount    = 0;
uint32_t noticeUntil    = 0;
bool     faceHold       = false;   // stay on the face page (updateBuddyMood)
const char *faceSub     = nullptr; // takes turns with the face's caption (updateBuddyMood)
// Time since a stamp made with millis() | 1, which may lie 1 ms ahead.
static uint32_t millisSince(uint32_t stamp) { return (uint32_t)max<int32_t>((int32_t)(millis() - stamp), 0); }
static uint32_t secondsSince(uint32_t stamp) { return millisSince(stamp) / 1000; }
bool writeJsonAtomic(const char *path, JsonDocument &doc);
void daySave();
void dayRoll();
void todayShow(const char *title, uint32_t ms);
void timerSet(bool run, bool byPc);
void gameTap();
void knockStart();
extern volatile bool knockWatching;
extern volatile uint32_t knockCount, knockStarts;
extern volatile float knockRing[8];
bool gameRunning();
void gameFlush();
void flushChanged();
void gifShow(const char *name, uint32_t ms);
void noticeShow(const char *text);
bool sdTest(JsonDocument &req, JsonDocument &doc);
void odooFromPc(JsonDocument &body);
uint8_t  odooSub      = 0;       // the task shown on the Odoo page; a tap steps it
uint16_t gifCount     = 0;       // animations found in /gifs
uint16_t gifIndex     = 0;       // the one the GIF page plays; a visit or a tap steps it
void gifStop();
static constexpr uint32_t PC_STATE_MAX_AGE_MS = 180000UL;   // the link sends every 60 s
uint16_t touchStartX     = 0;
uint16_t touchStartY     = 0;
uint16_t touchLastX      = 0;
uint16_t touchLastY      = 0;
uint32_t touchStartMs    = 0;
uint8_t  touchMissFrames  = 0;
bool     touchMoved       = false;
bool     touchSwipeDone   = false;  // page swipe fired; ignore this press until release
bool     touchDark        = false;  // this press began on a dimmed screen. Decided once,
                                    // so a window edge can't split a press in two
bool     touchNightStepped = false; // this press changed the night level, so it doesn't wake
uint8_t  tapCount        = 0;
uint32_t lastTapMs        = 0;
uint32_t autoPageBannerUntil = 0;
uint32_t nextBlink       = 1400;
uint32_t blinkUntil      = 0;
uint32_t nextGlance      = 900;
uint32_t nextAutoPage    = PAGE_AUTO_INTERVAL_MS;
uint8_t  brightnessLevel = BRIGHTNESS_DEFAULT;
uint32_t brightnessBannerUntil = 0;
uint8_t  savedBrightness       = BRIGHTNESS_DEFAULT;  // level in /display.json
bool     brightnessSavePending = false;
uint32_t brightnessChangedMs   = 0;
bool     nightActive     = false;   // inside the night window (needs a trusted clock)
bool     nightWoken      = false;   // a tap at night lifts the dimming...
uint32_t nightWokenAt    = 0;       // ...for NIGHT_WAKE_MS from this millis()
uint8_t  backlightOut    = 0;       // level last written to the backlight PWM
bool     nightBanner      = false;  // the brightness banner shows the night level
bool     nightSavePending = false;  // a swiped night level is waiting for flushNightSave()
uint8_t  savedNightBrightness = 0;  // night level in /config.json, or the default if none
bool     configReplaced   = false;  // a SET or DEFAULTS replaced /config.json: `settings`
                                    // no longer matches it, so no self-save before a restart
uint32_t lastSerialMs   = 0;
uint32_t clockStartMillis   = 0;
uint32_t clockStartSeconds  = 0;
int32_t  clockStartDays     = 0;   // days-from-civil at last clock seed (NTP or compile)
uint32_t lastNtpSyncMs      = 0;
uint32_t lastNtpAttemptMs    = 0;
float restAx    = 0.0f;
float restAy    = 0.0f;
float filteredAx = 0.0f;
float filteredAy = 0.0f;
float filteredGz = 0.0f;
float faceGlanceX  = 0.0f;
float faceGlanceY  = 0.0f;
float faceTargetX  = 0.0f;
float faceTargetY  = 0.0f;
float pressPulse   = 0.0f;

// ── Network data ──────────────────────────────────────────
// Fetch results live in netShared, written only after a successful fetch and
// only under netMutex. Once per frame the loop copies netShared into netView,
// and the draw functions read netView only, so a frame never mixes old and new
// values. Plain values only (no String), so a copy never touches the heap.
struct WeatherData { bool valid; int temp, humidity, wind, code; bool isDay; uint32_t updatedAt; };
struct StockData   { bool valid; float price, open, high, low, prevClose; uint32_t updatedAt; };
// valid: followers and repos are known. haveStars and haveActivity: so are the
// values of the two slower requests, which can fail on their own.
static const int GH_HEAT_WEEKS = 26;   // columns of the heatmap view
struct GithubData {
  bool valid, haveStars, haveActivity;
  int  followers, repos, stars, streak, today;
  // Contributions a day for the heatmap, oldest first, starting on a Sunday.
  // heatDays of them are real: the newest week stops at today.
  uint8_t heat[GH_HEAT_WEEKS * 7];
  uint8_t heatDays;
  uint32_t updatedAt;       // when all three parts were last fetched
};
struct CoinData {
  bool     valid, notFound;
  double   price;                 // a float keeps ~7 digits; BTC's cents need more
  float    open, high, low, ref;  // ref: Coinbase 24 h open, Finnhub previous close
  uint8_t  chartCount;            // Coinbase only; 0 = no chart yet
  float    chart[24];             // hourly closes, oldest first
  uint32_t updatedAt, chartAt;
};
// One quote per stock slot (indexed like settings.stocks), so tapping back to
// a stock shows its last value at once.
static const uint8_t ODOO_TASK_MAX = 6;
struct OdooTask { int id; char name[64], stage[32]; };
static const uint8_t ODOO_MEETING_MAX = 3;
struct OdooMeeting { char name[40]; int64_t start, stop; };   // UTC seconds
static const uint8_t ODOO_BEFORE_MAX = 3;
// Hours logged today and the open tasks.
struct OdooData {
  bool     valid;
  float    hours;
  float    hoursBefore;             // logged on the working day before today
  uint8_t  beforeWday;              // that day: 0 Sunday .. 6 Saturday
  char     beforeTasks[ODOO_BEFORE_MAX][40];   // my tasks changed that day, the last first
  uint8_t  beforeTaskCount;
  int      open, review, dev;
  OdooTask tasks[ODOO_TASK_MAX];   // changed last first
  uint8_t  taskCount;
  OdooMeeting meetings[ODOO_MEETING_MAX];   // mine from now to 24 h ahead, the next first
  uint8_t  meetingCount;
  int      activities;              // my activities due today or overdue
  uint32_t updatedAt;
};
struct NetData     { WeatherData weather; StockData stocks[SETTINGS_STOCK_MAX]; GithubData github; CoinData coins[SETTINGS_COIN_MAX]; OdooData odoo; };

NetData           netShared = {};
NetData           netView   = {};
SemaphoreHandle_t netMutex  = nullptr;

// The stock slots that are on, in slot order, built once in setup(): taps,
// fetching, the entry dots and drawing use only these.
uint8_t          stockActive[SETTINGS_STOCK_MAX];
uint8_t          stockActiveCount = 0;
volatile uint8_t stockSub         = 0;   // position in stockActive; read by netTask
uint8_t          stockFetchIdx    = 0;   // the slot fetchStock() asks for (netTask only)

void buildStockList() {
  stockActiveCount = 0;
  for (uint8_t i = 0; i < settings.stockCount && i < SETTINGS_STOCK_MAX; i++)
    if (settings.stocks[i].on) stockActive[stockActiveCount++] = i;
  if (stockSub >= stockActiveCount) stockSub = 0;
}

// The slot of the stock on screen.
static uint8_t shownStock() { return stockActive[stockActiveCount ? stockSub % stockActiveCount : 0]; }

// The coin slots that can be shown: on, and Coinbase or (Finnhub with a key).
uint8_t          coinActive[SETTINGS_COIN_MAX];
uint8_t          coinActiveCount = 0;
volatile uint8_t coinSub         = 0;   // position in coinActive; read by netTask
uint8_t          coinFetchIdx    = 0;   // the slot fetchCoin() asks for (netTask only)

void buildCoinList() {
  coinActiveCount = 0;
  for (uint8_t i = 0; i < settings.coinCount && i < SETTINGS_COIN_MAX; i++) {
    const Coin &c = settings.coins[i];
    if (c.on && (c.source == COIN_COINBASE || settings.stockKey[0] != '\0')) coinActive[coinActiveCount++] = i;
  }
  if (coinSub >= coinActiveCount) coinSub = 0;
}
static uint8_t shownCoin() { return coinActive[coinActiveCount ? coinSub % coinActiveCount : 0]; }
bool coinConfigured() { return coinActiveCount > 0; }

// Store one fetch result for the UI. The lock covers only this copy.
template <typename T>
void publishNetData(T &slot, const T &value) {
  if (!netMutex) return;
  xSemaphoreTake(netMutex, portMAX_DELAY);
  slot = value;
  xSemaphoreGive(netMutex);
}

// Copy the latest fetch results for this frame's drawing.
void snapshotNetData() {
  if (!netMutex) return;
  xSemaphoreTake(netMutex, portMAX_DELAY);
  netView = netShared;
  xSemaphoreGive(netMutex);
}

// Themed colours, assigned once by applyTheme() from settings.theme.
uint16_t COL_TIME, COL_SEC, COL_DATE_WD, COL_DATE_BIG, COL_VALUE, COL_LABEL;
uint16_t COL_TEMP, COL_WICON, COL_WSUB, COL_MOON, COL_MOONLAB;
uint16_t COL_PRICE_UP, COL_PRICE_DOWN, COL_TICKER, COL_GHLOGO, COL_GHNUM;

// ── Helpers ────────────────────────────────────────────────

uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Assign the themed colours from settings.theme. Called once at boot.
// Everything defaults to classic white (FG) so theme 0 reproduces the
// original look exactly; each theme below overrides the roles it colours.
void applyTheme() {
  uint16_t W = FG;
  COL_TIME=COL_SEC=COL_DATE_WD=COL_DATE_BIG=COL_VALUE=COL_LABEL=W;
  COL_TEMP=COL_WICON=COL_WSUB=W;  COL_MOON=W;  COL_MOONLAB=W;
  COL_PRICE_UP=COL_PRICE_DOWN=COL_TICKER=COL_GHLOGO=COL_GHNUM=W;
  switch (settings.theme) {
    case 1: {
      uint16_t CY=rgb(0,200,255), GRAY=rgb(120,130,140);
      COL_SEC=CY; COL_DATE_WD=CY; COL_TEMP=CY; COL_MOONLAB=CY;
      COL_PRICE_UP=COL_PRICE_DOWN=CY; COL_GHNUM=CY;
      COL_LABEL=GRAY; COL_WSUB=GRAY; COL_MOON=rgb(220,220,220);
      break;
    }
    case 2: {
      uint16_t GRAY=rgb(120,130,140);
      COL_SEC=rgb(255,180,40); COL_DATE_WD=rgb(255,180,40);
      COL_TEMP=rgb(255,140,60); COL_WICON=rgb(255,210,90); COL_WSUB=rgb(60,200,200);
      COL_MOON=rgb(255,210,90); COL_MOONLAB=rgb(0,200,255);
      COL_PRICE_UP=rgb(60,220,120); COL_PRICE_DOWN=rgb(255,80,90);
      COL_GHNUM=rgb(60,220,120); COL_LABEL=GRAY;
      break;
    }
    case 3: {
      uint16_t AM=rgb(255,180,40), GO=rgb(255,210,90), CR=rgb(245,225,190);
      COL_TIME=AM; COL_SEC=GO; COL_DATE_WD=AM; COL_DATE_BIG=CR;
      COL_VALUE=rgb(235,215,180); COL_LABEL=rgb(150,120,80);
      COL_TEMP=AM; COL_WICON=GO; COL_WSUB=rgb(180,150,110);
      COL_MOON=GO; COL_MOONLAB=AM;
      COL_PRICE_UP=COL_PRICE_DOWN=AM; COL_TICKER=CR; COL_GHLOGO=CR; COL_GHNUM=AM;
      break;
    }
    case 4: {
      uint16_t CY=rgb(0,200,255), BL=rgb(90,170,255), TE=rgb(60,200,200),
               SL=rgb(150,160,185), LG=rgb(205,220,235);
      COL_TIME=CY; COL_SEC=TE; COL_DATE_WD=BL; COL_DATE_BIG=LG;
      COL_VALUE=LG; COL_LABEL=SL;
      COL_TEMP=CY; COL_WICON=BL; COL_WSUB=TE;
      COL_MOON=rgb(210,225,245); COL_MOONLAB=CY;
      COL_PRICE_UP=COL_PRICE_DOWN=CY; COL_TICKER=LG; COL_GHLOGO=LG; COL_GHNUM=CY;
      break;
    }
    default: break;   // 0 = Classic: everything stays white
  }
}

float clampFloat(float v, float lo, float hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

// True once millis() has reached `deadline`. Comparing the signed difference
// keeps working across the millis() wrap at ~49.7 days of uptime, where a
// plain `now > deadline` stops the blink, glance and auto-advance timers.
static inline bool timeReached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}

// ── LCD init sequence for the AXS15231B panel ─────────────

void lcdRegInit() {
  static const uint8_t ops[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x11,
      END_WRITE,
      DELAY, 120,
      BEGIN_WRITE,
      WRITE_C8_D16, 0xDF, 0x98, 0x53,
      WRITE_C8_D8,  0xB2, 0x23,
      WRITE_COMMAND_8, 0xB7,
      WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
      WRITE_COMMAND_8, 0xBB,
      WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
      WRITE_C8_D16, 0xC0, 0x44, 0xA4,
      WRITE_C8_D8,  0xC1, 0x16,
      WRITE_COMMAND_8, 0xC3,
      WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
      WRITE_COMMAND_8, 0xC4,
      WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
      WRITE_COMMAND_8, 0xC8,
      WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      WRITE_COMMAND_8, 0xD0,
      WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
      WRITE_C8_D16, 0xD7, 0x00, 0x30,
      WRITE_C8_D8,  0xE6, 0x14,
      WRITE_C8_D8,  0xDE, 0x01,
      WRITE_COMMAND_8, 0xB7,
      WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
      WRITE_COMMAND_8, 0xC1,
      WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
      WRITE_C8_D16, 0xC2, 0x06, 0x3A,
      WRITE_C8_D16, 0xC4, 0x72, 0x12,
      WRITE_C8_D8,  0xBE, 0x00,
      WRITE_C8_D8,  0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3, 0x01, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x35, 0x00,
      WRITE_C8_D8, 0x3A, 0x05,
      WRITE_COMMAND_8, 0x2A,
      WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
      WRITE_COMMAND_8, 0x2B,
      WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
      WRITE_C8_D8, 0xDE, 0x02,
      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3, 0x00, 0x02, 0x00,
      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x36, 0x00,
      WRITE_COMMAND_8, 0x21,
      END_WRITE,
      DELAY, 10,
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x29,
      END_WRITE};
  bus->batchOperation(ops, sizeof(ops));
}

// ── Compile-time clock seed ────────────────────────────────

uint32_t compileTimeSeconds() {
  const char *t = __TIME__;
  uint8_t hh = (t[0]-'0')*10 + (t[1]-'0');
  uint8_t mm = (t[3]-'0')*10 + (t[4]-'0');
  uint8_t ss = (t[6]-'0')*10 + (t[7]-'0');
  return (uint32_t)hh*3600UL + (uint32_t)mm*60UL + ss;
}

uint8_t compileMonthNumber() {
  const char *m = __DATE__;
  static const char names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (uint8_t i = 0; i < 12; i++)
    if (strncmp(m, names+i*3, 3) == 0) return i+1;
  return 1;
}

int32_t daysFromCivil(int32_t y, uint8_t mo, uint8_t d) {
  y -= mo <= 2;
  const int32_t era = (y >= 0 ? y : y-399)/400;
  const uint32_t yoe = (uint32_t)(y - era*400);
  const uint32_t doy = (153*(mo+(mo>2?-3:9))+2)/5 + d - 1;
  const uint32_t doe = yoe*365 + yoe/4 - yoe/100 + doy;
  return era*146097 + (int32_t)doe - 719468;
}

void civilFromDays(int32_t z, int32_t *year, uint8_t *month, uint8_t *day) {
  z += 719468;
  const int32_t era = (z >= 0 ? z : z-146096)/146097;
  const uint32_t doe = (uint32_t)(z - era*146097);
  const uint32_t yoe = (doe - doe/1460 + doe/36524 - doe/146096)/365;
  int32_t y = (int32_t)yoe + era*400;
  const uint32_t doy = doe - (365*yoe + yoe/4 - yoe/100);
  const uint32_t mp  = (5*doy+2)/153;
  const uint32_t d   = doy - (153*mp+2)/5 + 1;
  const uint32_t mo  = mp + (mp < 10 ? 3 : -9);
  y += mo <= 2;
  *year  = y;
  *month = (uint8_t)mo;
  *day   = (uint8_t)d;
}

int32_t compileDateDays() {
  const char *d = __DATE__;
  uint8_t day = (d[4]==' ' ? 0 : d[4]-'0')*10 + (d[5]-'0');
  int32_t y = (int32_t)(d[7]-'0')*1000 + (int32_t)(d[8]-'0')*100 +
              (int32_t)(d[9]-'0')*10   + (d[10]-'0');
  return daysFromCivil(y, compileMonthNumber(), day);
}

// Fallback clock until NTP syncs. __DATE__/__TIME__ are the build machine's
// clock (UTC on the release workflow), converted through the time zone rule.
// Needs TZ set (setup() does that before calling this).
void seedClockFromCompileTime() {
  time_t t = (time_t)compileDateDays() * 86400 + (time_t)compileTimeSeconds();
  struct tm lt;
  localtime_r(&t, &lt);
  clockStartSeconds = (uint32_t)lt.tm_hour * 3600UL + (uint32_t)lt.tm_min * 60UL + lt.tm_sec;
  clockStartDays    = daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// The local wall-clock time now, for the clock, date and moon pages.
// Once NTP has synced, it reads the system clock, which SNTP keeps right and
// the TZ rule turns into local time, so a daylight-saving change shows the
// moment it happens. Before that, it runs on from the compile-time seed.
// days = days since 1970-01-01 of the local date.
struct LocalNow { uint8_t hour, min, sec, wday, month, mday; int32_t year, days; };

LocalNow localNow() {
  LocalNow n;
  struct tm lt;
  time_t t = time(nullptr);
  if (clockTrusted && localtime_r(&t, &lt) != nullptr && lt.tm_year > 100) {
    n.hour = lt.tm_hour; n.min = lt.tm_min; n.sec = lt.tm_sec;
    n.year = lt.tm_year + 1900; n.month = lt.tm_mon + 1; n.mday = lt.tm_mday;
    n.days = daysFromCivil(n.year, n.month, n.mday);
  } else {
    uint32_t elapsed = (millis() - clockStartMillis) / 1000UL;
    uint32_t total   = clockStartSeconds + elapsed;
    uint32_t sod     = total % 86400UL;
    n.hour = sod / 3600UL; n.min = (sod / 60UL) % 60UL; n.sec = sod % 60UL;
    n.days = clockStartDays + (int32_t)(total / 86400UL);
    civilFromDays(n.days, &n.year, &n.month, &n.mday);
  }
  n.wday = (uint8_t)(((n.days % 7) + 11) % 7);   // 1970-01-01 was a Thursday (4)
  return n;
}

// ── Drawing primitives ────────────────────────────────────

void centeredText(const char *text, int y, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(FG);
  int width = (int)strlen(text)*6*size;
  gfx->setCursor((SCREEN_W-width)/2, y);
  gfx->print(text);
}

void centeredTextColor(const char *text, int y, uint8_t size, uint16_t color) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int width = (int)strlen(text)*6*size;
  gfx->setCursor((SCREEN_W-width)/2, y);
  gfx->print(text);
}

void drawPageDots() {
  if (pageCount == 0) return;
  int startX = SCREEN_W/2 - ((pageCount-1)*16)/2;
  for (uint8_t i = 0; i < pageCount; i++) {
    if (i == currentPageIdx)
      gfx->fillCircle(startX+i*16, SCREEN_H-12, 3, FG);
    else
      gfx->drawCircle(startX+i*16, SCREEN_H-12, 2, rgb(90,90,90));
  }
  // Entry dots on the right edge on a page with several entries (spec D9):
  // filled = the stock shown, hollow = the others, like the page dots.
  const uint8_t entries = currentApp == PAGE_STOCK ? stockActiveCount : currentApp == PAGE_COIN ? coinActiveCount : 0;
  const uint8_t shown   = currentApp == PAGE_STOCK ? stockSub : coinSub;
  if (entries >= 2) {
    for (uint8_t i = 0; i < entries; i++) {
      const int y = subDotY(i, entries, SCREEN_H);
      if (i == shown % entries) gfx->fillCircle(SCREEN_W - 8, y, 3, FG);
      else                      gfx->drawCircle(SCREEN_W - 8, y, 2, rgb(90, 90, 90));
    }
  }
}

void drawHeader(const char *title) {
  // Page headings and the divider line were removed by request. This now just
  // clears the page; the title argument is kept so callers stay unchanged.
  (void)title;
  gfx->fillScreen(BG);
}

// Simple three-line boot/status card, used while Wi-Fi is being set up.
// Center text, auto-shrinking the font only if needed so it fits the width.
void centeredFit(const char *text, int y, uint8_t maxSize) {
  uint8_t sz = maxSize;
  int len = (int)strlen(text);
  while (sz > 1 && len * 6 * sz > SCREEN_W - 12) sz--;
  centeredText(text, y, sz);
}

void drawBootMessage(const char *l1, const char *l2, const char *l3) {
  gfx->fillScreen(BG);
  if (l1) centeredFit(l1, 36, 4);   // headline, up to size 4
  if (l2) centeredFit(l2, 90, 3);   // up to size 3
  if (l3) centeredFit(l3, 128, 2);  // up to size 2
  gfx->flush();
  flushForced = true;
}

// ── App navigation ────────────────────────────────────────

// The order pages are shown in is separate from their storage index:
// settings.pageOrder ("page_order") first, then every page it leaves out in
// the usual order (lib/settings: settingsRound(), SETTINGS_ROUND_DEFAULT).
// The usual order of the first eight, as SETTINGS_ROUND_DEFAULT starts (the coin
// page comes after the stock page). The installer's preview keeps the same
// list (web/js/preview.js SHOWN_ORDER; web-tests/preview.test.mjs compares).
[[maybe_unused]] static const uint8_t PAGE_SHOW_ORDER[] = {PAGE_FACE, PAGE_CLOCK, PAGE_DATE, PAGE_WEATHER,
                                                           PAGE_MOON, PAGE_STOCK, PAGE_COIN, PAGE_GITHUB};
static_assert(sizeof(PAGE_SHOW_ORDER) == SETTINGS_PAGE_COUNT, "every page has a place in the order");
static_assert(PAGE_ABDA == 8 && PAGE_ODOO == 9 && PAGE_PRS == 10 && PAGE_GIFS == 11 && PAGE_TODAY == 12
              && PAGE_GAME == 13 && PAGE_CLAUDE == 14, "settings.morePages[] and page_order use the PAGE_* ids");
static_assert(SETTINGS_ROUND_COUNT == APP_COUNT + 7, "pageOrder[] holds the whole round");

// Build the list of enabled pages, in the round's order, from settings.pages
// and settings.morePages. If none of the first eight pages is left (the ones
// that always have something to show), the Face page goes first, so the
// screen is never left blank.
void buildPageList() {
  uint8_t round[SETTINGS_ROUND_COUNT];
  settingsRound(settings, round);
  pageCount = 0;
  bool anyAlways = false;
  for (uint8_t n = 0; n < SETTINGS_ROUND_COUNT; n++) {
    const uint8_t i = round[n];
    if (i < SETTINGS_PAGE_COUNT) {
      if (!settings.pages[i]) continue;
      if (i == PAGE_COIN && !coinConfigured()) continue;      // nothing to show hides the page
      if (i == PAGE_GITHUB && !githubConfigured()) continue;  // "" hides the page
      if (i == PAGE_STOCK && !stockConfigured()) continue;    // no key hides the page
      anyAlways = true;
    } else if (!settings.morePages[i - SETTINGS_PAGE_COUNT]) {
      continue;
    }
    pageOrder[pageCount++] = i;
  }
  if (!anyAlways) {   // never leave it empty
    memmove(pageOrder + 1, pageOrder, pageCount);
    pageOrder[0] = PAGE_FACE;
    pageCount++;
  }
  if (currentPageIdx >= pageCount) currentPageIdx = 0;
  currentApp = pageOrder[currentPageIdx];
}

// Move to the next/previous ENABLED page (wraps around the enabled list).
void switchApp(int8_t delta, bool swiped) {
  if (pageCount == 0) return;
  if (todayUntil) { todayUntil = yesterdayUntil = 0; todayTitle = "TODAY"; }   // a swipe or a tap ends a briefing
  for (uint8_t tries = 0; tries < pageCount; tries++) {
    currentPageIdx = (currentPageIdx + pageCount + delta) % pageCount;
    const uint8_t page = pageOrder[currentPageIdx];
    if ((page != PAGE_PRS || (pc.known && pc.prCount > 0)) && (page != PAGE_GIFS || gifCount > 0)
        && (page != PAGE_ODOO || netView.odoo.valid) && (page != PAGE_CLAUDE || pc.known)
        && ((page != PAGE_GAME && page != PAGE_TODAY) || swiped)) break;
  }
  currentApp = pageOrder[currentPageIdx];
  if (currentApp == PAGE_GIFS) gifIndex++;
  pressPulse = 1.0f;
  nextAutoPage = millis() + PAGE_AUTO_INTERVAL_MS;
}

// ── Wi-Fi ─────────────────────────────────────────────────
// Credentials now come from AyresWiFiManager (LittleFS /wifi.json),
// entered through the captive portal. No compile-time SSID/password.

// True when the device has usable stored credentials.
// AWM's tieneCredenciales() checks that /wifi.json exists AND that a
// non-empty ssid+password were parsed from it at begin().
bool wifiConfigured() { return wifiManager.tieneCredenciales(); }
bool githubConfigured() { return settings.githubUser[0] != '\0'; }
bool stockConfigured() { return settings.stockKey[0] != '\0' && stockActiveCount > 0; }

// Network fetches call this before doing anything. It is a pure status
// check — AWM owns connecting and reconnecting. It refuses while the captive
// portal is up, because the radio is then in AP / AP_STA mode.
// Called from netTask as well as loop(): isPortalActive() only reads a bool,
// and isConnected() reads WiFi.status() (task-safe) and writes AWM's
// `connected` flag, which loop() also writes with the same derived value.
// Benign; goes away with the AWM upgrade.
bool ensureWifi() {
  if (wifiManager.isPortalActive()) return false;
  return wifiManager.isConnected();
}

// Open the portal on demand and log why.
void openSetupPortal(const char *reason) {
  if (wifiManager.isPortalActive()) return;
  Serial.printf("Opening Wi-Fi setup portal (%s)\n", reason);
  wifiManager.openPortal();
  // Cancel any in-flight touch gesture so the portal screen starts clean.
  touchWasDown = false;
  touchMoved   = false;
  touchMissFrames = 0;
  touchPortalArmed = false;
  // The finger that triggered this is still on the glass — make the portal
  // screen wait for a release before it will accept a "hold to close".
  portalNeedsRelease = true;
}

// ── NTP time sync ─────────────────────────────────────────
// The time zone is settings.tzRule, a POSIX rule such as
// "CET-1CEST,M3.5.0,M10.5.0/3", so daylight saving switches by itself.
//
// AyresWiFiManager's own time sync is switched off in setup()
// (setTimeSync(false)), so this is the only code that touches the clock.

volatile bool ntpStartWanted = false;   // syncNTP() asks, netTask starts the request

// Starts the time request. Runs on netTask, which looks the servers up and
// hands SNTP their addresses. Given names, SNTP asks the DNS by itself, and a
// fetch that begins while that question is open crashes the chip: hostByName()
// clears the DNS cache outside lwIP's lock ("assert failed: udp_new_ip_type").
static void startNtp() {
  static const char *const NAMES[] = {"pool.ntp.org", "time.cloudflare.com"};
  static char servers[2][16];
  for (uint8_t i = 0; i < 2; i++) {
    IPAddress address;
    if (WiFi.hostByName(NAMES[i], address) == 1) snprintf(servers[i], sizeof(servers[i]), "%s", address.toString().c_str());
  }
  if (!servers[0][0] && !servers[1][0]) return;   // no answer yet: syncNTP() asks again in a minute
  // configTzTime() sets TZ to the POSIX rule (daylight saving included).
  // configTime() with an offset would overwrite TZ with a fixed one.
  configTzTime(settings.tzRule, servers[0][0] ? servers[0] : servers[1], servers[1][0] ? servers[1] : servers[0]);
}

void syncNTP() {
  if (ntpSynced) return;
  if (!wifiManager.isConnected()) return;

  // NTP answers later: check every loop, restart the request only every 60 s.
  bool startOrRetry = lastNtpAttemptMs == 0
                   || millis() - lastNtpAttemptMs >= NTP_RETRY_INTERVAL_MS;
  if (startOrRetry) {
    lastNtpAttemptMs = millis();
    ntpStartWanted = true;
  }

  // The system clock survives esp_restart() and flashing, so a plausible year
  // proves nothing. Only trust it once SNTP reports a completed sync. The
  // status is read-and-clear: it reports COMPLETED once, which is enough here
  // because we stop asking after that. startNtp() starts SNTP over, so the
  // status is pending again after every (re)start.
  struct tm ti;
  memset(&ti, 0, sizeof(ti));
  if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED
      || !getLocalTime(&ti, 0) || ti.tm_year <= 100) {
    if (startOrRetry) Serial.println("NTP: pending — using compile-time seed");
    return;
  }

  // Convert to seconds-of-day (0–86399) for the running clock, plus the civil
  // day number so the date page rolls over from the real NTP date rather than
  // the compile date.
  uint32_t sod = (uint32_t)ti.tm_hour * 3600UL
               + (uint32_t)ti.tm_min  * 60UL
               + (uint32_t)ti.tm_sec;
  clockStartMillis  = millis();
  clockStartSeconds = sod;
  clockStartDays    = daysFromCivil(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  ntpSynced    = true;
  clockTrusted = true;
  lastNtpSyncMs = millis();
  Serial.printf("NTP synced: %02d:%02d:%02d (%s / %s)\n",
                ti.tm_hour, ti.tm_min, ti.tm_sec, settings.tzRule, settings.tzName);
}

// ── Weather fetch (Open-Meteo) ────────────────────────────

const char *weatherCodeText(int code) {
  if (code == 0)                             return "CLEAR";
  if (code == 1 || code == 2)               return "PARTLY CLOUDY";
  if (code == 3)                             return "CLOUDY";
  if (code == 45 || code == 48)             return "FOG";
  if ((code>=51&&code<=67)||(code>=80&&code<=82)) return "RAIN";
  if (code >= 71 && code <= 77)             return "SNOW";
  if (code >= 95)                            return "STORM";
  return "WEATHER";
}

void drawWeatherIcon(int cx, int cy, int code, bool isDay, uint16_t color) {
  if (code == 0) {
    gfx->drawCircle(cx, cy, 22, color);
    for (uint8_t i = 0; i < 8; i++) {
      float a = i*0.7854f;
      gfx->drawLine(cx+(int)(cos(a)*30), cy+(int)(sin(a)*30),
                    cx+(int)(cos(a)*40), cy+(int)(sin(a)*40), color);
    }
    if (!isDay) gfx->fillCircle(cx+11, cy-8, 18, BG);
    return;
  }
  gfx->fillCircle(cx-19, cy+5, 19, color);
  gfx->fillCircle(cx+2,  cy-6, 25, color);
  gfx->fillCircle(cx+27, cy+8, 17, color);
  gfx->fillRoundRect(cx-42, cy+8, 87, 25, 12, color);
  if ((code>=51&&code<=67)||(code>=80&&code<=82)) {
    for (int x=-25; x<=25; x+=17) {
      gfx->drawLine(cx+x, cy+45, cx+x-8, cy+62, color);
      gfx->drawLine(cx+x+1, cy+45, cx+x-7, cy+62, color);
    }
  } else if (code>=71 && code<=77) {
    for (int x=-24; x<=24; x+=24) {
      gfx->drawLine(cx+x-6, cy+53, cx+x+6, cy+53, color);
      gfx->drawLine(cx+x, cy+47, cx+x, cy+59, color);
      gfx->drawLine(cx+x-5, cy+48, cx+x+5, cy+58, color);
      gfx->drawLine(cx+x+5, cy+48, cx+x-5, cy+58, color);
    }
  }
}

bool fetchWeather() {
  if (!ensureWifi()) return false;
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();     // verify the server against the Mozilla roots
  client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  // '+' means a space in a URL query, so encode it (Etc/GMT+8, +48.1).
  String latEnc = settings.lat;
  latEnc.replace("+", "%2B");
  String lonEnc = settings.lon;
  lonEnc.replace("+", "%2B");
  String tzEncoded = String(settings.tzName);
  tzEncoded.replace("/", "%2F");
  tzEncoded.replace("+", "%2B");
  String weatherUrl = String("https://api.open-meteo.com/v1/forecast?")
    + "latitude=" + latEnc + "&longitude=" + lonEnc
    + "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
    + "&temperature_unit=" + (settings.tempUnit == TEMP_FAHRENHEIT ? "fahrenheit" : "celsius")
    + "&wind_speed_unit=" + (settings.windUnit == WIND_MPH ? "mph" : "kmh")
    + "&timezone=" + tzEncoded;
  if (!http.begin(client, weatherUrl))
    return false;
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;
  WeatherData w;
  w.temp      = (int)round(doc["current"]["temperature_2m"].as<float>());
  w.humidity  = doc["current"]["relative_humidity_2m"].as<int>();
  w.wind      = (int)round(doc["current"]["wind_speed_10m"].as<float>());
  w.code      = doc["current"]["weather_code"].as<int>();
  w.isDay     = doc["current"]["is_day"].as<int>() != 0;
  w.updatedAt = millis();
  w.valid     = true;
  publishNetData(netShared.weather, w);
  return true;
}

// ── Stock fetch (Finnhub JSON) ────────────────────────────────────
// Uses Finnhub's /quote endpoint, which returns a flat JSON object:
//   {"c":327.74,"d":1.15,"dp":0.3521,"h":329.6,"l":322.2204,
//    "o":323.13,"pc":326.59,"t":1784664000}
//   c  = current/last price   o  = open      h = high    l = low
//   d  = abs change           dp = % change  pc = prev close   t = epoch
// NOTE: settings.stockKey holds the Finnhub API token. Never log it.

bool fetchStock() {
  if (!ensureWifi()) return false;
  const uint8_t slot = stockFetchIdx;
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();   // verify the server against the Mozilla roots
  client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  String url = String("https://finnhub.io/api/v1/quote?symbol=") + settings.stocks[slot].symbol
             + "&token=" + settings.stockKey;
  if (!http.begin(client, url))
    return false;
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) return false;

  // "c" is the last price. Finnhub returns c=0 for an unknown symbol or an
  // empty/invalid response, so treat non-positive as a failed fetch.
  if (doc["c"].isNull()) return false;
  float last = doc["c"].as<float>();
  if (last <= 0.0f) return false;

  StockData s;
  s.price     = last;                    // "c" → last price
  s.open      = doc["o"].as<float>();    // "o" → open
  s.high      = doc["h"].as<float>();    // "h" → high
  s.low       = doc["l"].as<float>();    // "l" → low
  s.prevClose = doc["pc"].as<float>();   // "pc" → previous close (0 if missing)
  s.updatedAt = millis();
  s.valid     = true;
  publishNetData(netShared.stocks[slot], s);
  return true;
}

// ── Coin fetch ────────────────────────────────────────────
// Coinbase Exchange (no key): /products/<pair>/stats for the price, and
// /candles?granularity=3600 for the 24 h chart, scanned in place by
// parseCandleCloses (a response is about 21 KB). Finnhub (key) gives the same
// quote shape as a stock, for COINBASE:<pair>, but no chart.

// One HTTPS GET into `body`. False on any failure; *status gets the HTTP code
// (0 if no response), so a 404 can be told apart.
static bool coinbaseGet(const char *url, String &body, int *status) {
  *status = 0;
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();   // verify the server against the Mozilla roots
  client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setUserAgent("Monitor-Buddy/" FW_VERSION);   // Coinbase rejects requests without one
  if (!http.begin(client, url)) return false;
  *status = http.GET();
  if (*status != HTTP_CODE_OK) { http.end(); return false; }
  body = http.getString();
  http.end();
  return body.length() > 0;
}

// The coin on screen (coinFetchIdx), from its source. Unknown pair -> notFound.
// NOTE: settings.stockKey holds the Finnhub API token. Never log it.
bool fetchCoin() {
  if (!ensureWifi()) return false;
  const uint8_t slot = coinFetchIdx;
  const Coin &c = settings.coins[slot];
  CoinData d = netShared.coins[slot];   // netTask is the only writer
  if (c.source == COIN_FINNHUB) {
    // Same reply as a stock quote; all zeros for a pair Finnhub doesn't know.
    WiFiClientSecure client; client.useBuiltinCACertBundle(); client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
    HTTPClient http; http.setTimeout(HTTP_TIMEOUT_MS); http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    String url = String("https://finnhub.io/api/v1/quote?symbol=COINBASE:") + c.pair + "&token=" + settings.stockKey;
    if (!http.begin(client, url)) return false;
    if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, http.getString());
    http.end();
    if (err) return false;
    const double last = doc["c"] | 0.0;
    if (last <= 0.0) { d.notFound = true; publishNetData(netShared.coins[slot], d); return true; }
    d.price = last; d.open = doc["o"] | 0.0f; d.high = doc["h"] | 0.0f; d.low = doc["l"] | 0.0f;
    d.ref = doc["pc"] | 0.0f;
  } else {
    char url[96];
    snprintf(url, sizeof url, "https://api.exchange.coinbase.com/products/%s/stats", c.pair);
    String body; int status = 0;
    if (!coinbaseGet(url, body, &status)) {
      if (status == 404) { d.notFound = true; publishNetData(netShared.coins[slot], d); return true; }
      return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    const double last = strtod(doc["last"] | "0", nullptr);
    if (last <= 0.0) return false;
    d.price = last;
    d.open = d.ref = strtof(doc["open"] | "0", nullptr);
    d.high = strtof(doc["high"] | "0", nullptr);
    d.low  = strtof(doc["low"]  | "0", nullptr);
    // The chart changes hourly; it needs the clock for its time range.
    if (clockTrusted && (d.chartCount == 0 || millis() - d.chartAt >= COIN_CHART_REFRESH_MS)) {
      char from[24], to[24];
      const long long now = (long long)time(nullptr);
      if (formatIsoUtc(now - 24LL * 3600, from, sizeof from) && formatIsoUtc(now, to, sizeof to)) {
        char curl[192];
        snprintf(curl, sizeof curl, "https://api.exchange.coinbase.com/products/%s/candles?granularity=3600&start=%s&end=%s", c.pair, from, to);
        String candles; int cs = 0;
        if (coinbaseGet(curl, candles, &cs)) {
          float pts[24];
          const uint8_t n = parseCandleCloses(candles.c_str(), pts, 24);
          if (n >= 2) { memcpy(d.chart, pts, n * sizeof(float)); d.chartCount = n; d.chartAt = millis(); }
          else Serial.println("[net] coin: candles response not understood");
        }
      }
    }
  }
  d.updatedAt = millis();
  d.valid = true;
  publishNetData(netShared.coins[slot], d);
  return true;
}

// ── GitHub fetch ──────────────────────────────────────────

// Hands an HTTP body to a scanner as it arrives, so a 200 kB response never
// sits in RAM. HTTPClient::writeToStream() also undoes chunked encoding.
template <typename Scan>
class ScanSink : public Stream {
 public:
  explicit ScanSink(Scan &s) : scan(s) {}
  size_t write(uint8_t b) override { scan.feed(&b, 1); return 1; }
  size_t write(const uint8_t *buf, size_t n) override { scan.feed(buf, n); return n; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
 private:
  Scan &scan;
};

// GET `url` from GitHub. The body goes to `scan`, or into `body` when given.
template <typename Scan>
static bool githubGet(const String &url, Scan *scan, String *body) {
  WiFiClientSecure client;
  client.useBuiltinCACertBundle();   // verify the server against the Mozilla roots
  client.setHandshakeTimeout(TLS_HANDSHAKE_TIMEOUT_S);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "ESP32-C6-Touch-LCD");
  if (http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  bool ok = true;
  if (body) {
    *body = http.getString();
  } else {
    ScanSink<Scan> sink(*scan);
    ok = http.writeToStream(&sink) > 0;   // negative: the connection dropped mid-body
  }
  http.end();
  return ok;
}

// Three public, token-free requests: the profile (followers, repos), the repo
// list (stars) and the contribution calendar (streak, today). A part that
// succeeded is not asked for again until all three are in, so a retry costs
// one request, not three: GitHub allows 60 API requests an hour per address.
// Runs on netTask only.
bool fetchGithub() {
  if (!githubConfigured()) return false;
  if (!ensureWifi()) return false;
  static GithubData g = {};
  static bool userDone = false, starsDone = false, activityDone = false;
  const String user = settings.githubUser;

  if (!userDone) {
    String body;
    JsonDocument doc;
    if (githubGet<ghscan::StarScan>("https://api.github.com/users/" + user, nullptr, &body)
        && !deserializeJson(doc, body)) {
      g.followers = doc["followers"].as<int>();
      g.repos     = doc["public_repos"].as<int>();
      g.valid     = true;
      userDone    = true;
    }
  }
  if (!starsDone) {
    // ponytail: first 100 repos only; follow the Link header if anyone has more.
    ghscan::StarScan scan;
    if (githubGet("https://api.github.com/users/" + user + "/repos?per_page=100&type=owner", &scan,
                  (String *)nullptr)) {
      g.stars     = (int)scan.stars;
      g.haveStars = true;
      starsDone   = true;
    }
  }
  if (!activityDone) {
    static ghscan::ContribScan scan;   // 400 bytes of day counts: keep it off the task stack
    scan = ghscan::ContribScan();
    // A page with far fewer days is an error or a changed layout, not a calendar.
    if (githubGet("https://github.com/users/" + user + "/contributions", &scan, (String *)nullptr)
        && scan.days() >= 300) {
      g.streak       = scan.streak();
      g.today        = scan.today();
      const long newest  = scan.newestDay();
      const int  weekday = (int)((newest + 4) % 7);   // 1970-01-01 was a Thursday; 0 = Sunday
      const long start   = newest - weekday - (GH_HEAT_WEEKS - 1) * 7;
      g.heatDays = (uint8_t)((GH_HEAT_WEEKS - 1) * 7 + weekday + 1);
      for (int i = 0; i < g.heatDays; i++) g.heat[i] = (uint8_t)scan.at(start + i);
      g.haveActivity = true;
      activityDone   = true;
    }
  }

  const bool all = userDone && starsDone && activityDone;
  if (all) {
    g.updatedAt = millis();
    userDone = starsDone = activityDone = false;   // next refresh asks for all three again
  }
  // Publish partial results too: the page shows what it has.
  if (g.valid) publishNetData(netShared.github, g);
  Serial.printf("[net] github followers %d repos %d stars %d streak %d today %d%s\n",
                g.followers, g.repos, g.stars, g.streak, g.today, all ? "" : " (partial)");
  return all;
}

// Odoo, as Chrome on the PC reads it with my signed-in session
// (tools/chrome-odoo), over the link or USB:
//   {"h": hours today, "hb": hours on the working day before, "bw": its weekday,
//    "y": [name, ...] my tasks changed that day, the last first,
//    "o": open tasks, "r": in code review, "d": in development, "a": activities due,
//    "t": [[id, name, stage], ...] changed last first,
//    "m": [[name, start, stop], ...] my meetings of the next 24 h, UTC seconds}
void odooFromPc(JsonDocument &body) {
  OdooData d = {};
  d.hours       = body["h"] | 0.0f;
  d.hoursBefore = body["hb"] | 0.0f;
  d.beforeWday  = (body["bw"] | 1) % 7;
  for (JsonVariantConst name : body["y"].as<JsonArrayConst>()) {
    if (d.beforeTaskCount >= ODOO_BEFORE_MAX) break;
    char *slot = d.beforeTasks[d.beforeTaskCount++];
    odooAscii(name | "", slot, sizeof(d.beforeTasks[0]), sizeof(d.beforeTasks[0]) - 1);
  }
  d.open        = body["o"] | 0;
  d.review      = body["r"] | 0;
  d.dev         = body["d"] | 0;
  d.activities  = body["a"] | 0;
  for (JsonVariantConst task : body["t"].as<JsonArrayConst>()) {
    if (d.taskCount >= ODOO_TASK_MAX) break;
    OdooTask &slot = d.tasks[d.taskCount++];
    char stage[64];
    slot.id = task[0] | 0;
    odooAscii(task[1] | "", slot.name, sizeof(slot.name), sizeof(slot.name) - 1);
    odooStage(task[2] | "", stage, sizeof(stage));
    odooAscii(stage, slot.stage, sizeof(slot.stage), sizeof(slot.stage) - 1);
  }
  for (JsonVariantConst meeting : body["m"].as<JsonArrayConst>()) {
    if (d.meetingCount >= ODOO_MEETING_MAX) break;
    OdooMeeting &slot = d.meetings[d.meetingCount++];
    odooAscii(meeting[0] | "", slot.name, sizeof(slot.name), sizeof(slot.name) - 1);
    slot.start = meeting[1] | 0LL;
    slot.stop  = meeting[2] | 0LL;
  }
  d.valid     = true;
  d.updatedAt = millis() | 1;
  if (netView.odoo.valid && netView.odoo.hours < ODOO_DAY_HOURS && d.hours >= ODOO_DAY_HOURS) gifShow(GIF_FULL_DAY, 9000);
  publishNetData(netShared.odoo, d);
}

// -- Face rendering (DESKBUDDY-style expressive eyes) -------
// Ported and scaled up from DESKBUDDY-1.0 (Edison Science Corner):
//   https://github.com/EDISON-SCIENCE-CORNER/DESKBUDDY-1.0
// The original targets a 128x64 mono OLED; here everything is scaled
// ~2.5x for this 320x172 colour panel and driven by the same spring
// physics: springy eyes, laggy pupils, blink, saccades and breathing.
// IMU tilt is folded into the gaze so the eyes still follow the board.

// Emotion particle bitmaps (16x16, 1-bit), drawn scaled 2x.
static const unsigned char bmp_heart[] PROGMEM = {
  0x00,0x00,0x0c,0x60,0x1e,0xf0,0x3f,0xf8,0x7f,0xfc,0x7f,0xfc,0x7f,0xfc,0x3f,0xf8,
  0x1f,0xf0,0x0f,0xe0,0x07,0xc0,0x03,0x80,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const unsigned char bmp_zzz[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x00,0x0c,0x00,0x18,0x00,0x30,0x00,0x7e,
  0x00,0x00,0x3c,0x00,0x0c,0x00,0x18,0x00,0x30,0x00,0x7c,0x00,0x00,0x00,0x00,0x00 };
static const unsigned char bmp_anger[] PROGMEM = {
  0x00,0x00,0x11,0x10,0x2a,0x90,0x44,0x40,0x80,0x20,0x80,0x20,0x44,0x40,0x2a,0x90,
  0x11,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };



// Eye geometry on the 320x172 canvas (centres, not top-left).
static const float FACE_LEFT_CX  = 105.0f;
static const float FACE_RIGHT_CX = 215.0f;
static const float FACE_EYES_CY  = 80.0f;

// One eye: animated CENTRE (x,y) + size (w,h), plus a pupil that lags behind.
// Each quantity is a critically-ish damped spring toward its target.
struct Eye {
  float x, y, w, h;
  float targetX, targetY, targetW, targetH;
  float pupilX, pupilY, targetPupilX, targetPupilY;
  float velX = 0, velY = 0, velW = 0, velH = 0, pVelX = 0, pVelY = 0;
  float k  = 0.12f;   // eye spring
  float d  = 0.60f;   // eye damping (heavier feel)
  float pk = 0.08f;   // pupil spring (softer/laggier)
  float pd = 0.50f;   // pupil damping
  bool  blinking = false;
  unsigned long lastBlink = 0, nextBlinkTime = 0;

  void init(float _x, float _y, float _w, float _h) {
    x = targetX = _x; y = targetY = _y;
    w = targetW = _w; h = targetH = _h;
    pupilX = targetPupilX = 0; pupilY = targetPupilY = 0;
    nextBlinkTime = millis() + random(1000, 4000);
  }
  void update() {
    velX = (velX + (targetX - x) * k) * d;
    velY = (velY + (targetY - y) * k) * d;
    velW = (velW + (targetW - w) * k) * d;
    velH = (velH + (targetH - h) * k) * d;
    x += velX; y += velY; w += velW; h += velH;
    pVelX = (pVelX + (targetPupilX - pupilX) * pk) * pd;
    pVelY = (pVelY + (targetPupilY - pupilY) * pk) * pd;
    pupilX += pVelX; pupilY += pVelY;
  }
};

Eye leftEye, rightEye;
static unsigned long lastSaccade     = 0;
static unsigned long saccadeInterval = 3000;
static float gazeLX = 0.0f, gazeLY = 0.0f;   // current saccade gaze target
static float breathVal = 0.0f;

// Draw a 1-bit bitmap scaled by an integer factor (nearest neighbour).
void drawBitmapScaled(int x, int y, const uint8_t *bmp, int w, int h, int scale, uint16_t color) {
  int bytesPerRow = (w + 7) / 8;
  for (int row = 0; row < h; row++)
    for (int col = 0; col < w; col++) {
      uint8_t b = pgm_read_byte(bmp + row * bytesPerRow + (col >> 3));
      if (b & (0x80 >> (col & 7)))
        gfx->fillRect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

// Black "eyelid" fills that carve each mood's expression into the sclera.
// Proportional to eye size so they scale with the mood shapes.
void drawEyelidMask(int ix, int iy, int iw, int ih, uint8_t mood, bool isLeft) {
  int slant = (int)(iw * 0.167f);
  int band  = (int)(ih * 0.44f);
  if (mood == MOOD_ANGRY || mood == MOOD_FOCUS || mood == MOOD_CI) { // angry: lids lowered towards the nose (a frown)
    for (int i = 0; i < band; i++)
      if (isLeft) gfx->drawLine(ix, iy - slant + i, ix + iw, iy + i,         BG);
      else        gfx->drawLine(ix, iy + i,         ix + iw, iy - slant + i, BG);
  } else if (mood == MOOD_SAD || mood == MOOD_NUDGE) { // sad: lids lowered at the outer corners (a droop)
    for (int i = 0; i < band; i++)
      if (isLeft) gfx->drawLine(ix, iy + i,         ix + iw, iy - slant + i, BG);
      else        gfx->drawLine(ix, iy - slant + i, ix + iw, iy + i,         BG);
  } else if (mood == MOOD_SLEEPY) {               // heavy top lids
    gfx->fillRect(ix, iy, iw, ih / 2 + 5, BG);
  } else if (mood == MOOD_SUSPICIOUS) {           // one squint, one wide
    if (isLeft) gfx->fillRect(ix, iy, iw, ih / 2 - 5, BG);
    else        gfx->fillRect(ix, iy + ih - (int)(ih * 0.22f), iw, (int)(ih * 0.22f), BG);
  } else if (mood == MOOD_UNIMPRESSED) {          // flat lids, half shut, with a line along their edge
    gfx->fillRect(ix, iy, iw, (int)(ih * 0.46f), BG);
    gfx->fillRect(ix - 6, iy + (int)(ih * 0.46f) - 4, iw + 12, 5, rgb(150, 160, 175));
  }
}

// A robot's eyes are little displays: dark scan lines across them.
static void faceScanLines(int ix, int iy, int iw, int ih) {
  if (settings.face != FACE_ROBOT) return;
  for (int y = iy + 5; y < iy + ih - 1; y += 6) gfx->drawFastHLine(ix, y, iw, BG);
}

// What a face style adds around the eyes, drawn before them: a cat's ears,
// nose and whiskers; a robot's eye frames and side bolts. The ears and bolts
// sway with the gaze but do not blink.
static void drawFaceExtras(const Eye &left, const Eye &right) {
  const uint16_t grey = rgb(150, 160, 175), pink = rgb(255, 120, 150);
  if (settings.face == FACE_CAT) {
    for (int side = 0; side < 2; side++) {
      const int ex = (int)(side ? right.x : left.x), out = side ? 1 : -1;
      gfx->fillTriangle(ex + out * 44, 46, ex + out * 34, 4, ex + out * 2, 38, FG);
      gfx->fillTriangle(ex + out * 36, 36, ex + out * 31, 14, ex + out * 14, 34, pink);
      const int wx = side ? 272 : 48;                        // whiskers
      for (int line = 0; line < 2; line++) {
        gfx->drawLine(wx, 104 + line * 10, wx + out * 34, 98 + line * 16, grey);
        gfx->drawLine(wx, 105 + line * 10, wx + out * 34, 99 + line * 16, grey);
      }
    }
    const int nx = (int)((left.x + right.x) / 2);
    gfx->fillTriangle(nx - 6, 100, nx + 6, 100, nx, 108, pink);   // the nose, between the eyes
  } else if (settings.face == FACE_ROBOT) {
    for (int side = 0; side < 2; side++) {
      const Eye &eye = side ? right : left;
      const int bx = side ? (int)eye.x + 62 : (int)eye.x - 72;
      gfx->fillRoundRect(bx, 66, 10, 28, 3, grey);          // a bolt at each side of the head
      gfx->fillRect(bx + 3, 72, 4, 16, BG);
      if (faceMood != MOOD_AWAY)                            // a frame around each little display
        gfx->drawRect((int)(eye.x - eye.w / 2) - 4, (int)(eye.y - eye.h / 2) - 4, (int)eye.w + 8, (int)eye.h + 8, grey);
    }
  }
}

// The party face: confetti falls behind the eyes, and a hat goes on top of the right one.
static void drawConfetti() {
  static const uint8_t COLOURS[][3] = {{255, 99, 71}, {255, 210, 60}, {120, 220, 120}, {90, 170, 255}, {203, 166, 189}};
  for (int piece = 0; piece < 26; piece++) {
    const uint8_t *c = COLOURS[piece % 5];
    const int x = (piece * 53 + 17) % SCREEN_W, y = (int)((millis() / (9 + piece % 4 * 3) + piece * 41) % 150) - 10;   // above the caption
    if (piece % 2) gfx->fillRect(x, y, 8, 5, rgb(c[0], c[1], c[2]));
    else           gfx->fillCircle(x, y, 4, rgb(c[0], c[1], c[2]));
  }
}

static void drawPartyHat(const Eye &right) {
  const int hx = (int)right.x + 8, base = max(44, (int)(right.y - right.h / 2) + 12);   // tilted a little, its tip on screen
  gfx->fillTriangle(hx - 30, base - 4, hx + 34, base + 6, hx + 16, base - 42, rgb(90, 170, 255));
  gfx->fillTriangle(hx - 14, base - 16, hx + 27, base - 10, hx + 21, base - 24, rgb(255, 210, 60));   // a stripe
  gfx->fillCircle(hx + 16, base - 40, 6, rgb(255, 99, 71));                                            // the pompom
}

// A hand waving at the right edge, for the hello at the end of setup.
static void drawWave() {
  const float angle = sinf(millis() / 170.0f) * 0.42f;
  const float px = 286, py = 136;                         // the wrist
  const int cx = (int)(px + sinf(angle) * 46), cy = (int)(py - cosf(angle) * 46);
  for (int i = 0; i <= 6; i++)                              // the arm
    gfx->fillCircle((int)(px + (cx - px) * i / 6), (int)(py + (cy - py) * i / 6), 5, FG);
  gfx->fillCircle(cx, cy, 15, FG);                          // the palm
  static const float SPREAD[] = {-0.55f, -0.18f, 0.18f, 0.55f};
  for (float spread : SPREAD) {                             // four fingers
    const float a = angle + spread;
    for (int i = 0; i <= 4; i++) gfx->fillCircle(cx + (int)(sinf(a) * (14 + i * 5)), cy - (int)(cosf(a) * (14 + i * 5)), 4, FG);
  }
  const float thumb = angle - 1.35f;
  for (int i = 0; i <= 2; i++) gfx->fillCircle(cx + (int)(sinf(thumb) * (13 + i * 4)), cy - (int)(cosf(thumb) * (13 + i * 4)), 4, FG);
}

// The pupil in the face style's shape (setting "face"): a rounded square, a
// cat's upright slit (rounder when the pupil is big), or a robot's hard square.
static void drawPupil(int px, int py, int pw, int ph, int round) {
  if (settings.face == FACE_CAT) {
    const int rx = pw > 44 ? pw / 3 : max(4, pw / 5);
    gfx->fillEllipse(px + pw / 2, py + ph / 2, rx, ph / 2 + 3, BG);
  } else if (settings.face == FACE_ROBOT) {
    gfx->fillRect(px + 2, py + 2, pw - 4, ph - 4, BG);
  } else {
    gfx->fillRoundRect(px, py, pw, ph, round, BG);
  }
}

// A glint on the pupil: a dot, or a robot's square pixel.
static void drawGlint(int x, int y, int r, uint16_t colour) {
  if (settings.face == FACE_ROBOT) gfx->fillRect(x - r, y - r, 2 * r, 2 * r, colour);
  else                             gfx->fillCircle(x, y, r, colour);
}

// Draw one eye: white sclera, laggy black pupil, glint, then the eyelid mask.
// The face style changes the outline (classic rounded, cat oval, robot square
// with scan lines, minimal a slim pill without a pupil); the moods stay the same.
void drawEyeShape(Eye &e, bool isLeft) {
  const uint8_t style = settings.face;
  int iw = (int)e.w, ih = (int)e.h;
  if (iw < 2 || ih < 2) return;
  int ix = (int)(e.x - e.w / 2.0f);
  if (style == FACE_MINIMAL) {   // a slim pill around the same centre
    iw = max(2, iw * 11 / 20);
    ix = (int)(e.x - iw / 2.0f);
  }
  int iy = (int)(e.y - e.h / 2.0f);

  if (faceMood == MOOD_AWAY) {                   // shut, rising and falling with the breath
    gfx->fillRoundRect(ix, (int)(e.y - 4 + breathVal), iw, 8, 4, FG);
    return;
  }

  int r  = (iw < 50) ? 6 : 16;
  if (style == FACE_ROBOT) r = 3;
  else if (style == FACE_CAT) r = min(iw, ih) * 9 / 20;
  else if (style == FACE_MINIMAL) r = min(iw, ih) / 2;
  // Angry = red eyes (hard-coded face colour, not affected by the theme).
  uint16_t sclera = (faceMood == MOOD_ANGRY || faceMood == MOOD_CI) ? rgb(255, 80, 90) : FG;
  gfx->fillRoundRect(ix, iy, iw, ih, r, sclera);

  int cx = ix + iw / 2, cy = iy + ih / 2;
  int pw = (int)(iw / 2.2f), ph = (int)(ih / 2.2f);

  if (style == FACE_MINIMAL && faceMood != MOOD_LOVE && faceMood != MOOD_DIZZY) {   // no pupil: the shape and the lids say it all
    if (faceMood == MOOD_EXCITED || faceMood == MOOD_PARTY) {   // the sparkle stays
      const int sx = isLeft ? (ix - 6) : (ix + iw + 6), sy = iy + 2;
      gfx->fillRect(sx - 7, sy - 1, 15, 3, rgb(255, 210, 60));
      gfx->fillRect(sx - 1, sy - 7, 3, 15, rgb(255, 210, 60));
    }
    if (faceMood == MOOD_NUDGE && isLeft) {
      const int ty = iy + ih / 3 + (int)((millis() % 1600) * (ih * 2 / 3) / 1600);
      gfx->fillTriangle(ix - 10, ty - 9, ix - 15, ty, ix - 5, ty, rgb(90, 170, 255));
      gfx->fillCircle(ix - 10, ty, 5, rgb(90, 170, 255));
    }
    drawEyelidMask(ix - 1, iy - 1, iw + 2, ih + 1, faceMood, isLeft);   // a pixel wider: the pill's round ends
    return;
  }

  if (faceMood == MOOD_UNIMPRESSED) {   // small pupils that sit right under the lids: a stare, not a glance
    const int lid = iy + (int)(ih * 0.46f);
    gfx->fillRoundRect(cx - 15, lid - 16, 30, 36, 10, BG);
    gfx->fillCircle(cx + 6, lid + 6, 3, FG);
    drawEyelidMask(ix, iy, iw, ih, faceMood, isLeft);
    return;
  }

  if (faceMood == MOOD_DIZZY) {   // a spiral of two and a half turns spins where the pupil was, each eye its own way
    const float turn = millis() / 130.0f * (isLeft ? 1.0f : -1.0f), reach = min(iw, ih) * 0.40f;
    for (int i = 6; i <= 60; i++) {
      const float along = i / 60.0f, angle = along * 15.7f + turn;
      gfx->fillCircle(cx + (int)(cosf(angle) * reach * along), cy + (int)(sinf(angle) * reach * along), 3, BG);
    }
    return;
  }

  if (faceMood == MOOD_HAPPY || faceMood == MOOD_PARTY) {
    // Bright, cheerful open eyes: normal pupil, twin glints, no eyelid mask.
    // Rosy blush cheeks are added in drawFace().
    int px = cx + (int)e.pupilX - pw / 2;
    int py = cy + (int)e.pupilY - ph / 2;
    if (px < ix) px = ix;
    if (px + pw > ix + iw) px = ix + iw - pw;
    if (py < iy) py = iy;
    if (py + ph > iy + ih) py = iy + ih - ph;
    drawPupil(px, py, pw, ph, r / 2);
    drawGlint(px + pw - 10, py + 10, 5, FG);   // main glint
    drawGlint(px + 9, py + ph - 11, 3, FG);    // second glint
    faceScanLines(ix, iy, iw, ih);
    return;
  }

  if (faceMood == MOOD_EXCITED) {
    // Wide, shiny eyes: an oversized pupil, twin gold glints, and a gold sparkle
    // (hard-coded face colour, not affected by the theme).
    uint16_t gold = rgb(255, 210, 60);
    pw = (int)(iw / 1.85f); ph = (int)(ih / 1.85f);
    int px = cx + (int)e.pupilX - pw / 2;
    int py = cy + (int)e.pupilY - ph / 2;
    if (px < ix) px = ix;
    if (px + pw > ix + iw) px = ix + iw - pw;
    if (py < iy) py = iy;
    if (py + ph > iy + ih) py = iy + ih - ph;
    drawPupil(px, py, pw, ph, r / 2);
    drawGlint(px + pw - 10, py + 10, 5, gold);   // main glint (gold)
    drawGlint(px + 9, py + ph - 11, 3, gold);    // second glint (gold)
    faceScanLines(ix, iy, iw, ih);
    int sx = isLeft ? (ix - 2) : (ix + iw + 2);      // sparkle on the outer side
    int sy = iy + 2;
    gfx->fillRect(sx - 7, sy - 1, 15, 3, gold);
    gfx->fillRect(sx - 1, sy - 7, 3, 15, gold);
    return;
  }

  if (faceMood == MOOD_LOVE) {
    // Heart-shaped pupil: reuse the 16x16 heart icon, drawn in BG on the sclera,
    // scaled to the pupil size and following the same gaze offset. Eyes stay fully
    // open (no eyelid mask for LOVE), so both hearts are always visible.
    int scale = pw / 6;
    if (scale < 2) scale = 2;
    if (scale > 4) scale = 4;
    int hw = 16 * scale, hh = 16 * scale;
    int hx = cx + (int)e.pupilX - hw / 2;
    int hy = cy + (int)e.pupilY - hh / 2;
    if (hx < ix) hx = ix;
    if (hx + hw > ix + iw) hx = ix + iw - hw;
    if (hy < iy) hy = iy;
    if (hy + hh > iy + ih) hy = iy + ih - hh;
    drawBitmapScaled(hx, hy, bmp_heart, 16, 16, scale, BG);
  } else {
    int px = cx + (int)e.pupilX - pw / 2;
    int py = cy + (int)e.pupilY - ph / 2;
    if (px < ix) px = ix;
    if (px + pw > ix + iw) px = ix + iw - pw;
    if (py < iy) py = iy;
    if (py + ph > iy + ih) py = iy + ih - ph;
    drawPupil(px, py, pw, ph, r / 2);
    if (iw > 30 && ih > 30)
      drawGlint(px + pw - 10, py + 10, 5, faceMood == MOOD_FOCUS ? rgb(74, 195, 203) : FG);   // glint
  }
  faceScanLines(ix, iy, iw, ih);
  if (faceMood == MOOD_NUDGE && isLeft) {        // a tear down the outer side, once every 1.6 s
    const int ty = iy + ih / 3 + (int)((millis() % 1600) * (ih * 2 / 3) / 1600);
    gfx->fillTriangle(ix - 10, ty - 9, ix - 15, ty, ix - 5, ty, rgb(90, 170, 255));
    gfx->fillCircle(ix - 10, ty, 5, rgb(90, 170, 255));
  }

  drawEyelidMask(ix, iy, iw, ih, faceMood, isLeft);
}

// Advance springs, blink, saccades, breathing and the per-mood eye shapes.
// tiltX/tiltY are the IMU-derived tilt (-1..1) so the gaze tracks the board.
static void faceStep(float tiltX, float tiltY) {
  unsigned long now = millis();

  // Blink (both eyes together, off the left eye's timer).
  if (timeReached(now, leftEye.nextBlinkTime)) {
    leftEye.blinking = rightEye.blinking = true;
    leftEye.lastBlink = now;
    leftEye.nextBlinkTime = now + random(2000, 6000);
  }
  if (leftEye.blinking && now - leftEye.lastBlink > 120)
    leftEye.blinking = rightEye.blinking = false;

  // Saccade: occasionally jump the gaze to a new random direction.
  if (!leftEye.blinking && now - lastSaccade > saccadeInterval) {
    lastSaccade = now;
    saccadeInterval = random(500, 3000);
    int dir = random(0, 10);
    if      (dir < 4)  { gazeLX = 0;   gazeLY = 0;   }   // centre
    else if (dir == 4) { gazeLX = -15; gazeLY = -10; }   // TL
    else if (dir == 5) { gazeLX = 15;  gazeLY = -10; }   // TR
    else if (dir == 6) { gazeLX = -15; gazeLY = 10;  }   // BL
    else if (dir == 7) { gazeLX = 15;  gazeLY = 10;  }   // BR
    else if (dir == 8) { gazeLX = 20;  gazeLY = 0;   }   // R
    else               { gazeLX = -20; gazeLY = 0;   }   // L
  }

  // Gaze target = saccade offset + live IMU tilt, applied every frame.
  float pupX = gazeLX + tiltX * 20.0f;
  float pupY = gazeLY + tiltY * 12.0f;
  leftEye.targetPupilX  = rightEye.targetPupilX = pupX;
  leftEye.targetPupilY  = rightEye.targetPupilY = pupY;
  const float aside = faceWave ? -24.0f : 0.0f;   // room for the waving hand
  leftEye.targetX  = FACE_LEFT_CX  + aside + gazeLX * 0.3f + tiltX * 8.0f;
  rightEye.targetX = FACE_RIGHT_CX + aside + gazeLX * 0.3f + tiltX * 8.0f;
  leftEye.targetY  = rightEye.targetY = FACE_EYES_CY + gazeLY * 0.3f + tiltY * 6.0f;

  if (leftEye.blinking) {
    leftEye.targetH = rightEye.targetH = 4;                 // slam shut
  } else {
    switch (faceMood) {
      case MOOD_HAPPY:
      case MOOD_PARTY:
        // Big open eyes + blush cheeks (drawn in drawEyeShape / drawFace).
        leftEye.targetW = rightEye.targetW = 92;
        leftEye.targetH = rightEye.targetH = 92;
        leftEye.targetPupilY  -= 6;   // gaze slightly up = cheerful
        rightEye.targetPupilY -= 6;   break;
      case MOOD_LOVE:
        // Wide-open, fully-visible round eyes (heart pupils drawn in drawEyeShape).
        leftEye.targetW = rightEye.targetW = 96;
        leftEye.targetH = rightEye.targetH = 96;  break;
      case MOOD_SURPRISED:
        leftEye.targetW = rightEye.targetW = 75;
        leftEye.targetH = rightEye.targetH = 112;
        leftEye.targetPupilX  += random(-3, 4);             // jitter
        rightEye.targetPupilX += random(-3, 4); break;
      case MOOD_SLEEPY:
        leftEye.targetW = rightEye.targetW = 95;
        leftEye.targetH = rightEye.targetH = 75;  break;
      case MOOD_ANGRY:
        leftEye.targetW = rightEye.targetW = 85;
        leftEye.targetH = rightEye.targetH = 80;  break;
      case MOOD_SAD:
        leftEye.targetW = rightEye.targetW = 85;
        leftEye.targetH = rightEye.targetH = 100; break;
      case MOOD_EXCITED:
        // Tall, wide-open shiny eyes (big pupil + twin glints + sparkle).
        leftEye.targetW = rightEye.targetW = 90;
        leftEye.targetH = rightEye.targetH = 106 + breathVal;
        leftEye.targetPupilY  += random(-2, 3);
        rightEye.targetPupilY += random(-2, 3); break;
      case MOOD_FOCUS:
        leftEye.targetW = rightEye.targetW = 92;
        leftEye.targetH = rightEye.targetH = 58;
        // Reading: the pupils sweep left to right, then snap back for the next line.
        leftEye.targetPupilX = rightEye.targetPupilX = -14.0f + 28.0f * (float)(now % 2400UL) / 2400.0f;
        leftEye.targetPupilY = rightEye.targetPupilY = 0;   break;
      case MOOD_CI:
        leftEye.targetW = rightEye.targetW = 85;
        leftEye.targetH = rightEye.targetH = 80;
        leftEye.targetX  += random(-3, 4);                  // shaking
        rightEye.targetX += random(-3, 4); break;
      case MOOD_NUDGE:
        leftEye.targetW = rightEye.targetW = 85;
        leftEye.targetH = rightEye.targetH = 100;
        leftEye.targetPupilY  += 8;                         // looking down
        rightEye.targetPupilY += 8;
        leftEye.targetPupilX  += random(-2, 3);             // trembling
        rightEye.targetPupilX += random(-2, 3); break;
      case MOOD_DIZZY:
        leftEye.targetW = rightEye.targetW = 94;
        leftEye.targetH = rightEye.targetH = 94;
        // The head sways: both eyes go round, the right one a little behind.
        leftEye.targetX  += cosf(now / 110.0f) * 9;
        leftEye.targetY  += sinf(now / 110.0f) * 6;
        rightEye.targetX += cosf(now / 110.0f - 0.6f) * 9;
        rightEye.targetY += sinf(now / 110.0f - 0.6f) * 6; break;
      case MOOD_UNIMPRESSED:
        leftEye.targetW = rightEye.targetW = 96;
        leftEye.targetH = rightEye.targetH = 84;
        // A stare straight at you, whatever the tilt and the wandering gaze,
        // with a slow shake of the head.
        leftEye.targetX  = FACE_LEFT_CX + sinf(now / 300.0f) * 6;
        rightEye.targetX = FACE_RIGHT_CX + sinf(now / 300.0f) * 6;
        leftEye.targetY = rightEye.targetY = FACE_EYES_CY; break;
      case MOOD_SUSPICIOUS:
        leftEye.targetW  = 90;  leftEye.targetH  = 50;      // squint
        rightEye.targetW = 90;  rightEye.targetH = 105;     // wide
        break;
      case MOOD_NORMAL:
      default:
        leftEye.targetW = rightEye.targetW = 90;
        leftEye.targetH = rightEye.targetH = 90 + breathVal; break;
    }
  }

  leftEye.update();
  rightEye.update();
}

// The springs keep the step they were tuned at, FACE_STEP_MS. A frame between
// two steps shows the eyes that far along the way, so they glide at any frame rate.
#define FACE_STEP_MS 70
static Eye leftShown, rightShown;

static Eye eyeBetween(const Eye &from, const Eye &to, float along) {
  Eye eye = to;
  eye.x = from.x + (to.x - from.x) * along;
  eye.y = from.y + (to.y - from.y) * along;
  eye.w = from.w + (to.w - from.w) * along;
  eye.h = from.h + (to.h - from.h) * along;
  eye.pupilX = from.pupilX + (to.pupilX - from.pupilX) * along;
  eye.pupilY = from.pupilY + (to.pupilY - from.pupilY) * along;
  return eye;
}

void updateFacePhysics(float tiltX, float tiltY) {
  static uint32_t steppedAt = 0;
  static Eye leftBefore, rightBefore;
  const uint32_t now = millis();
  breathVal = sin(now / 800.0f) * 3.5f;
  if (steppedAt == 0 || now - steppedAt > 1000UL) {   // the face was off screen: no catching up
    steppedAt = now;
    leftBefore = leftEye;
    rightBefore = rightEye;
  }
  while (now - steppedAt >= FACE_STEP_MS) {
    steppedAt += FACE_STEP_MS;
    leftBefore = leftEye;
    rightBefore = rightEye;
    faceStep(tiltX, tiltY);
  }
  const float along = (now - steppedAt) / (float)FACE_STEP_MS;
  leftShown = eyeBetween(leftBefore, leftEye, along);
  rightShown = eyeBetween(rightBefore, rightEye, along);
}

void drawFace(float tx, float ty) {
  if (faceMood == MOOD_LUNCH) {   // a drawn picture: two frames of a chew, swapped every 260 ms
    // The canvas takes a non-const pointer; it only reads the pixels.
    gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)((millis() / 260) % 2 ? LUNCH_FACE_B : LUNCH_FACE_A), SCREEN_W, SCREEN_H);
    if (faceCaption) centeredTextColor(faceCaption, 148, 2, rgb(150, 160, 175));
    return;
  }
  if (faceMood == MOOD_ONAIR) {   // a studio sign, lit and dimmed in turn
    const uint16_t red = (millis() / 800) % 2 ? rgb(150, 24, 32) : rgb(235, 40, 52);
    gfx->fillScreen(0x0000);
    gfx->drawRoundRect(34, 20, 252, 96, 18, red);
    gfx->fillRoundRect(40, 26, 240, 84, 14, red);
    gfx->setTextSize(5);
    gfx->setTextColor(RGB565_WHITE);
    gfx->setCursor((SCREEN_W - 6 * 30) / 2 + 3, 26 + (84 - 35) / 2);
    gfx->print("ON AIR");
    if (faceCaption) centeredTextColor(faceCaption, 132, 2, rgb(150, 160, 175));
    return;
  }
  gfx->fillScreen(BG);
  updateFacePhysics(tx, ty);

  // Floating emotion particles (scaled 2x from DESKBUDDY's 16x16 icons).
  if (faceMood == MOOD_LOVE) {
    drawBitmapScaled(18,  8, bmp_heart, 16, 16, 2, FG);
    drawBitmapScaled(270, 8, bmp_heart, 16, 16, 2, FG);
  } else if (faceMood == MOOD_SLEEPY) {
    drawBitmapScaled(144, 6, bmp_zzz, 16, 16, 2, FG);   // centred between the eyes
  } else if (faceMood == MOOD_ANGRY) {
    drawBitmapScaled(144, 8, bmp_anger, 16, 16, 2, FG); // centred between the eyes
  } else if (faceMood == MOOD_AWAY) {                   // a z drifting up and right, every 2.4 s
    const int drift = (int)((millis() % 2400) / 100), late = (drift + 12) % 24;
    drawBitmapScaled(236 + drift, 40 - drift, bmp_zzz, 16, 16, 2, FG);
    drawBitmapScaled(262 + late, 52 - late, bmp_zzz, 16, 16, 1, FG);   // a smaller one, half a cycle behind
  } else if (faceMood == MOOD_CI && (millis() / 400) % 2 == 0) {
    gfx->fillRoundRect(155, 6, 10, 22, 3, rgb(255, 80, 90));
    gfx->fillCircle(160, 36, 5, rgb(255, 80, 90));
  } else if (faceMood == MOOD_DIZZY) {                  // three stars circle overhead
    for (int star = 0; star < 3; star++) {
      const float angle = millis() / 260.0f + star * 2.094f;
      const int sx = SCREEN_W / 2 + (int)(cosf(angle) * 92), sy = 13 + (int)(sinf(angle) * 7);
      gfx->fillRect(sx - 6, sy - 1, 13, 3, rgb(255, 210, 60));
      gfx->fillRect(sx - 1, sy - 6, 3, 13, rgb(255, 210, 60));
    }
  } else if (faceMood == MOOD_UNIMPRESSED) {            // "...", a dot more every half second
    for (int dot = 0; dot < (int)((millis() / 500) % 4); dot++) gfx->fillCircle(262 + dot * 13, 18, 4, rgb(150, 160, 175));
  }

  if (faceMood == MOOD_PARTY) drawConfetti();
  drawFaceExtras(leftShown, rightShown);
  drawEyeShape(leftShown, true);
  drawEyeShape(rightShown, false);
  if (faceMood == MOOD_PARTY) drawPartyHat(rightShown);

  // HAPPY: soft rosy blush cheeks at the lower-outer corners of the eyes.
  if ((faceMood == MOOD_HAPPY || faceMood == MOOD_PARTY) && settings.face != FACE_ROBOT) {   // a robot does not blush
    uint16_t blush = rgb(255, 120, 150);
    const int aside = faceWave ? -24 : 0;   // with the eyes, when they make room for the hand
    gfx->fillCircle(70 + aside,  126, 13, blush);
    gfx->fillCircle(250 + aside, 126, 13, blush);
  }
  if (faceWave) drawWave();

  // Small offline hint on the face page so the user knows setup is available.
  if (!wifiManager.isConnected()) {
    gfx->setTextSize(1);
    gfx->setTextColor(rgb(150,150,150));
    const char *msg = wifiConfigured() ? "WIFI OFFLINE" : "HOLD TO SET UP WIFI";
    int w = (int)strlen(msg)*6;
    gfx->setCursor((SCREEN_W-w)/2, 6);
    gfx->print(msg);
  }
  if (faceSub && (millis() / 2000) % 2) centeredTextColor(faceSub, 148, 2, rgb(74, 195, 203));
  else if (faceCaption) centeredTextColor(faceCaption, 148, 2, rgb(150, 160, 175));
  if (faceCaption && faceMood == MOOD_FOCUS && (millis() / 500) % 2 == 0)   // a typing cursor
    gfx->fillRect((SCREEN_W + (int)strlen(faceCaption) * 12) / 2 + 2, 160, 10, 3, rgb(74, 195, 203));
}

// ── 7-segment clock ───────────────────────────────────────

void drawDigitSegment(int x, int y, int w, int h, int t, uint8_t seg, uint16_t color) {
  int half = h/2, r = t/2;
  switch (seg) {
    case 0: gfx->fillRoundRect(x+t, y, w-2*t, t, r, color); break;
    case 1: gfx->fillRoundRect(x+w-t, y+t, t, half-t, r, color); break;
    case 2: gfx->fillRoundRect(x+w-t, y+half, t, half-t, r, color); break;
    case 3: gfx->fillRoundRect(x+t, y+h-t, w-2*t, t, r, color); break;
    case 4: gfx->fillRoundRect(x, y+half, t, half-t, r, color); break;
    case 5: gfx->fillRoundRect(x, y+t, t, half-t, r, color); break;
    case 6: gfx->fillRoundRect(x+t, y+half-t/2, w-2*t, t, r, color); break;
  }
}

void drawDigit(int x, int y, uint8_t digit, int w, int h, int t, uint16_t color) {
  static const uint8_t masks[10] = {
    0b00111111,0b00000110,0b01011011,0b01001111,0b01100110,
    0b01101101,0b01111101,0b00000111,0b01111111,0b01101111};
  for (uint8_t seg = 0; seg < 7; seg++)
    if (masks[digit%10] & (1<<seg)) drawDigitSegment(x, y, w, h, t, seg, color);
}

void drawClock() {
  const LocalNow now = localNow();
  uint8_t hh = now.hour;
  const uint8_t mm = now.min, ss = now.sec;
  const bool h12 = settings.clockFormat == CLOCK_12H_FORMAT;
  const bool pm = hh >= 12;
  if (h12) hh = hour12(hh);
  gfx->fillScreen(BG);
  const int W = 54, H = 100, T = 10, Y = 18;   // bigger 7-seg digits
  // HH:MM shifted left to make room for the seconds after the minutes.
  // 12-hour: no leading zero (" 9:41", not "09:41").
  if (!h12 || hh >= 10) drawDigit(8, Y, hh/10, W, H, T, COL_TIME);
  drawDigit(70,  Y, hh%10, W, H, T, COL_TIME);
  if (h12) {
    // AM/PM above the seconds, top-aligned with the digits.
    gfx->setTextSize(2); gfx->setTextColor(COL_SEC);
    gfx->setCursor(282, Y + 4);
    gfx->print(pm ? "PM" : "AM");
  }
  if ((ss%2)==0) {
    gfx->fillRoundRect(134, Y+28, 11, 11, 5, COL_TIME);
    gfx->fillRoundRect(134, Y+62, 11, 11, 5, COL_TIME);
  }
  drawDigit(154, Y, mm/10, W, H, T, COL_TIME);
  drawDigit(216, Y, mm%10, W, H, T, COL_TIME);
  // Seconds (same size 3) tucked in behind the minutes, bottom-aligned.
  char sbuf[4];
  snprintf(sbuf, sizeof(sbuf), "%02u", (unsigned)ss);
  gfx->setTextSize(3); gfx->setTextColor(COL_SEC);
  gfx->setCursor(278, Y + H - 24);
  gfx->print(sbuf);
  drawPageDots();
}

// ── Date page ─────────────────────────────────────────────

void drawDatePage() {
  static const char *wd[]  = {"SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"};
  static const char *mon[] = {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  const LocalNow now = localNow();
  int32_t year = now.year; uint8_t month = now.month, day = now.mday;
  uint8_t weekday = now.wday;
  gfx->fillScreen(BG);
  centeredTextColor(wd[weekday], 14, 4, COL_DATE_WD);
  char line[24];
  snprintf(line, sizeof(line), "%s %02u", mon[month-1], day);
  centeredTextColor(line, 62, 6, COL_DATE_BIG);
  snprintf(line, sizeof(line), "%ld", (long)year);
  centeredTextColor(line, 126, 3, COL_VALUE);
  drawPageDots();
}

// ── Shared "network page needs Wi-Fi" panel ───────────────
// Replaces the old "NO WIFI CONFIG / EDIT CONFIG" message, which told
// the user to edit a #define that no longer exists.
bool drawWifiGate() {
  if (wifiManager.isPortalActive()) {
    centeredText("SETUP MODE", 40, 4);
    centeredText("JOIN WIFI", 92, 2);
    centeredText(AP_SSID, 118, 2);
    drawPageDots();
    return true;
  }
  if (!wifiConfigured()) {
    centeredText("WIFI SETUP", 40, 4);
    centeredText("HOLD SCREEN 3s", 92, 2);
    centeredText("TO CONFIGURE", 118, 2);
    drawPageDots();
    return true;
  }
  if (!wifiManager.isConnected()) {
    centeredText("CONNECTING", 40, 4);
    centeredText("HOLD SCREEN 3s", 92, 2);
    centeredText("FOR WIFI SETUP", 118, 2);
    drawPageDots();
    return true;
  }
  return false;
}

// ── Weather page ──────────────────────────────────────────

void drawWeather() {
  gfx->fillScreen(BG);
  if (drawWifiGate()) return;
  const WeatherData &w = netView.weather;
  if (!w.valid) { centeredText("UPDATING", 74, 3); drawPageDots(); return; }
  drawWeatherIcon(250, 66, w.code, w.isDay, COL_WICON);
  gfx->setTextSize(8); gfx->setTextColor(COL_TEMP);
  gfx->setCursor(14, 34); gfx->print(w.temp);
  gfx->setTextSize(3); gfx->print(settings.tempUnit == TEMP_FAHRENHEIT ? "F" : "C");
  gfx->setTextSize(2); gfx->setTextColor(COL_WSUB);
  gfx->setCursor(14, 116); gfx->print(weatherCodeText(w.code));
  gfx->setTextColor(COL_LABEL);
  gfx->setCursor(14,  140); gfx->print("H "); gfx->print(w.humidity); gfx->print("%");
  // Wind, right-aligned: same 14 px gap from the right edge as humidity has from the left.
  { String wu = settings.windUnit == WIND_MPH ? "MPH" : "KMH";
    String windStr = String("W ") + w.wind + wu;
    int windW = (int)windStr.length() * 12;          // size-2 chars are 12 px wide
    gfx->setCursor(SCREEN_W - 14 - windW, 140);
    gfx->print(windStr); }
  drawPageDots();
}

// ── Moon phase page ───────────────────────────────────────

const char *moonPhaseLabel(float phase) {
  if (phase<0.03f||phase>0.97f) return "NEW MOON";
  if (phase<0.22f)              return "WAXING CRESCENT";
  if (phase<0.28f)              return "FIRST QUARTER";
  if (phase<0.47f)              return "WAXING GIBBOUS";
  if (phase<0.53f)              return "FULL MOON";
  if (phase<0.72f)              return "WANING GIBBOUS";
  if (phase<0.78f)              return "LAST QUARTER";
  return "WANING CRESCENT";
}

void drawMoonDisc(int cx, int cy, int radius, float phase, uint16_t color) {
  phase = phase - floor(phase);
  gfx->drawCircle(cx, cy, radius+3, rgb(72,72,72));
  gfx->fillCircle(cx, cy, radius, color);
  if (phase<0.03f||phase>0.97f) {
    gfx->fillCircle(cx, cy, radius-2, BG);
    gfx->drawCircle(cx, cy, radius, color);
    return;
  }
  if (phase>0.47f && phase<0.53f) return;
  int shadowX = (phase < 0.5f)
    ? cx - (int)(4.0f*radius*phase)
    : cx + (int)(2.0f*radius - 4.0f*radius*(phase-0.5f));
  gfx->fillCircle(shadowX, cy, radius, BG);
  gfx->drawCircle(cx, cy, radius, color);
}

void drawMoon() {
  const float syn = 29.53058867f;
  const LocalNow now = localNow();
  // Days since 1970 UTC; 10962.7597 is the 2000-01-06 18:14 UTC new moon on
  // that scale. Before NTP, the local seed is the best there is (hours off).
  double days = clockTrusted
      ? (double)time(nullptr) / 86400.0
      : (double)now.days + ((double)now.hour * 3600.0 + now.min * 60.0 + now.sec) / 86400.0;
  float age  = (float)fmod(days - 10962.7597, (double)syn);
  if (age < 0.0f) age += syn;
  float phase       = age/syn;
  int   illumination = (int)round((1.0f-cos(phase*6.2831853f))*50.0f);
  gfx->fillScreen(BG);
  drawMoonDisc(240, 74, 58, phase, COL_MOON);
  gfx->setTextSize(2); gfx->setTextColor(COL_MOONLAB);
  gfx->setCursor(10, 24);  gfx->print(moonPhaseLabel(phase));
  gfx->setTextColor(COL_VALUE);
  gfx->setCursor(10, 92);  gfx->print("AGE "); gfx->print(age, 1); gfx->print(" DAYS");
  gfx->setCursor(10, 120); gfx->print("LIGHT "); gfx->print(illumination); gfx->print("%");
  drawPageDots();
}

// ── Stock page ────────────────────────────────────────────

void drawStock() {
  gfx->fillScreen(BG);
  if (drawWifiGate()) return;
  const uint8_t slot = shownStock();
  const StockData &s = netView.stocks[slot];
  const char *symbol = settings.stocks[slot].symbol;
  if (!s.valid) { centeredText("UPDATING", 74, 3); drawPageDots(); return; }
  char pbuf[16];
  snprintf(pbuf, sizeof(pbuf), "$%.2f", s.price);
  // Up/down is measured against yesterday's close, like any quote screen;
  // the open is only a stand-in if Finnhub left "pc" out.
  const float ref = s.prevClose > 0.0f ? s.prevClose : s.open;
  const bool  up  = s.price >= ref;
  uint16_t priceCol = up ? COL_PRICE_UP : COL_PRICE_DOWN;
  centeredTextColor(pbuf, 26, 6, priceCol);      // big price (up=green/down=red in semantic)
  // Ticker symbol on the left. Size 4 (twice the Open/High/Low text) for
  // symbols up to 5 characters; 6+ characters shrink so the text still fits
  // the left column, which must end before the O/H/L block at x=150.
  int tlen  = (int)strlen(symbol);
  int tsize = 4;
  while (tsize > 1 && tlen * 6 * tsize > 132) tsize--;   // ~132 px available
  gfx->setTextSize(tsize); gfx->setTextColor(COL_TICKER);
  gfx->setCursor(14, (ref > 0.0f ? 112 : 124) - 4 * tsize);  // centred, with or without the % line
  gfx->print(symbol);
  // Day change in percent under the ticker, e.g. "+0.35%".
  if (ref > 0.0f) {
    char cbuf[12];
    snprintf(cbuf, sizeof(cbuf), "%+.2f%%", (s.price - ref) / ref * 100.0f);
    gfx->setTextSize(2); gfx->setTextColor(priceCol);
    gfx->setCursor(14, 132);
    gfx->print(cbuf);
  }
  // Open / High / Low on the right side.
  gfx->setTextSize(2); gfx->setTextColor(COL_VALUE);
  gfx->setCursor(150, 92);  gfx->print("OPEN  "); gfx->print(s.open, 2);
  gfx->setCursor(150, 116); gfx->print("HIGH  "); gfx->print(s.high, 2);
  gfx->setCursor(150, 140); gfx->print("LOW   "); gfx->print(s.low, 2);
  drawPageDots();
}

// ── GitHub page ───────────────────────────────────────────

// 5x7 glyphs the display font lacks, drawn at the font's scale (bit 4 = left).
static const uint8_t GLYPH_EURO[7]  = {0x07, 0x08, 0x1E, 0x08, 0x1E, 0x08, 0x07};
static const uint8_t GLYPH_POUND[7] = {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x1F};
static void drawGlyph5x7(const uint8_t *rows, int x, int y, uint8_t size, uint16_t color) {
  for (uint8_t r = 0; r < 7; r++)
    for (uint8_t c = 0; c < 5; c++)
      if (rows[r] & (0x10 >> c)) gfx->fillRect(x + c * size, y + r * size, size, size, color);
}
// The currency before a price: "$", a drawn £ or €, or "SGD ". Returns its width.
static int drawCurrencyMark(CurrencyMark m, const char *code, int x, int y, uint8_t size, uint16_t color) {
  if (m == CUR_EURO)  { drawGlyph5x7(GLYPH_EURO, x, y, size, color);  return 6 * size; }
  if (m == CUR_POUND) { drawGlyph5x7(GLYPH_POUND, x, y, size, color); return 6 * size; }
  gfx->setTextSize(size); gfx->setTextColor(color); gfx->setCursor(x, y);
  if (m == CUR_DOLLAR) { gfx->print('$'); return 6 * size; }
  gfx->print(code); gfx->print(' ');
  return (int)(strlen(code) + 1) * 6 * size;
}

// 24 h line of hourly closes plus the live price, scaled into the box, with a
// dim line at the reference (24 h open) when it falls inside the range.
static void drawCoinChart(const CoinData &d, bool haveRef, uint16_t col, int sx, int sy, int sw, int sh) {
  if (d.chartCount < 2) {
    gfx->setTextSize(1); gfx->setTextColor(COL_LABEL);
    gfx->setCursor(sx + 30, sy + sh / 2 - 4);
    gfx->print("CHART LOADING");
    return;
  }
  float pts[25];
  uint8_t n = d.chartCount > 24 ? 24 : d.chartCount;
  memcpy(pts, d.chart, n * sizeof(float));
  pts[n++] = (float)d.price;                  // end the line at the live price
  float lo = pts[0], hi = pts[0];
  for (uint8_t i = 1; i < n; i++) { lo = fminf(lo, pts[i]); hi = fmaxf(hi, pts[i]); }
  // Flat market: keep it on screen, padded in proportion to the price.
  if (hi - lo <= hi * 1e-6f) { const float pad = hi * 5e-4f + 1e-9f; hi += pad; lo -= pad; }
  if (haveRef && d.ref >= lo && d.ref <= hi) {
    const int ry = sy + sh - 1 - (int)lroundf((d.ref - lo) / (hi - lo) * (sh - 1));
    for (int x = sx; x < sx + sw; x += 4) gfx->drawPixel(x, ry, rgb(80, 80, 80));
  }
  int px = 0, py = 0;
  for (uint8_t i = 0; i < n; i++) {
    const int x = sx + (int)lroundf((float)i * (sw - 1) / (n - 1));
    const int y = sy + sh - 1 - (int)lroundf((pts[i] - lo) / (hi - lo) * (sh - 1));
    if (i > 0) {
      gfx->drawLine(px, py, x, y, col);
      gfx->drawLine(px, py + 1, x, y + 1, col);   // 2 px thick
    }
    px = x; py = y;
  }
  gfx->fillCircle(px, py, 3, col);                // the live price
}

void drawCoin() {
  gfx->fillScreen(BG);
  if (drawWifiGate()) return;
  const uint8_t slot = shownCoin();
  const Coin &c = settings.coins[slot];
  const CoinData &d = netView.coins[slot];
  if (d.notFound) {
    centeredText("NOT FOUND", 56, 3);
    centeredText(c.pair, 100, 2);
    drawPageDots(); return;
  }
  if (!d.valid) { centeredText("UPDATING", 74, 3); drawPageDots(); return; }

  // Finnhub measures against the previous close, falling back to the open.
  const float ref = (c.source == COIN_FINNHUB && d.ref <= 0.0f) ? d.open : d.ref;
  const bool  haveRef = ref > 0.0f;
  const bool  up = !haveRef || d.price >= ref;
  const uint16_t col = up ? COL_PRICE_UP : COL_PRICE_DOWN;

  // Price line: currency mark + number, the largest size that fits [0, 304).
  char num[24]; formatCoinPrice(d.price, num, sizeof num);
  char code[8]; const CurrencyMark m = currencyOf(c.pair, code, sizeof code);
  const int markUnits = (m == CUR_CODE) ? (int)strlen(code) + 1 : 1;
  const int numLen = (int)strlen(num);
  uint8_t psize = 6;
  while (psize > 3 && (markUnits + numLen) * 6 * psize > 304 - 16) psize--;
  const int lineW = (markUnits + numLen) * 6 * psize;
  const int py = 18 + (6 - psize) * 4;
  int x0 = (304 - lineW) / 2; if (x0 < 0) x0 = 0;
  const int mw = drawCurrencyMark(m, code, x0, py, psize, col);
  gfx->setTextSize(psize); gfx->setTextColor(col);
  gfx->setCursor(x0 + mw, py);
  gfx->print(num);

  // Base code: the part of the pair before '-'.
  char base[12];
  { size_t i = 0; while (c.pair[i] && c.pair[i] != '-' && i < sizeof(base) - 1) { base[i] = c.pair[i]; i++; } base[i] = '\0'; }

  char cbuf[12];
  snprintf(cbuf, sizeof(cbuf), "%+.2f%%", haveRef ? (d.price - ref) / ref * 100.0 : 0.0);

  if (c.source == COIN_COINBASE) {
    int bsize = 4;
    while (bsize > 1 && (int)strlen(base) * 6 * bsize > 112) bsize--;   // chart starts at x 132
    gfx->setTextSize(bsize); gfx->setTextColor(COL_TICKER);
    gfx->setCursor(14, 82 + (4 - bsize) * 4);
    gfx->print(base);
    if (haveRef) {
      gfx->setTextSize(2); gfx->setTextColor(col);
      gfx->setCursor(14, 120);
      gfx->print(cbuf);
    }
    if (d.high > 0.0f && d.low > 0.0f) {
      char hi[16], lo[16], line[40];
      formatCoinPrice(d.high, hi, sizeof hi);
      formatCoinPrice(d.low, lo, sizeof lo);
      snprintf(line, sizeof line, "H %s  L %s", hi, lo);
      gfx->setTextSize(1); gfx->setTextColor(COL_LABEL);
      gfx->setCursor(14, 144);
      gfx->print(line);
    }
    const int sx = 132, sy = 82, sw = SCREEN_W - 17 - sx, sh = 58;
    drawCoinChart(d, haveRef, col, sx, sy, sw, sh);
  } else {
    // Finnhub: the stock page's layout. Ref is the previous close, else the open.
    const float fref = ref;
    const uint16_t fcol = col;
    int tsize = 4;
    while (tsize > 1 && (int)strlen(base) * 6 * tsize > 132) tsize--;
    gfx->setTextSize(tsize); gfx->setTextColor(COL_TICKER);
    gfx->setCursor(14, (fref > 0.0f ? 112 : 124) - 4 * tsize);
    gfx->print(base);
    if (fref > 0.0f) {
      snprintf(cbuf, sizeof(cbuf), "%+.2f%%", (d.price - fref) / fref * 100.0);
      gfx->setTextSize(2); gfx->setTextColor(fcol);
      gfx->setCursor(14, 132);
      gfx->print(cbuf);
    }
    char vo[16], vh[16], vl[16];
    formatCoinPrice(d.open, vo, sizeof vo);
    formatCoinPrice(d.high, vh, sizeof vh);
    formatCoinPrice(d.low, vl, sizeof vl);
    size_t mx = strlen(vo); if (strlen(vh) > mx) mx = strlen(vh); if (strlen(vl) > mx) mx = strlen(vl);
    // Labels of 6 chars ("OPEN  ") plus the value must end before x 306.
    const uint8_t vsize = ((6 + (int)mx) * 12 <= 306 - 150) ? 2 : 1;
    gfx->setTextSize(vsize); gfx->setTextColor(COL_VALUE);
    const int ys[3] = {92, 116, 140};
    gfx->setCursor(150, ys[0]); gfx->print("OPEN  "); gfx->print(vo);
    gfx->setCursor(150, ys[1]); gfx->print("HIGH  "); gfx->print(vh);
    gfx->setCursor(150, ys[2]); gfx->print("LOW   "); gfx->print(vl);
  }
  drawPageDots();
}

// Ring animation round the profile picture. Tune to taste.
static constexpr float GH_ORBIT_MS       = 2600.0f;   // one lap of the comet
static constexpr float GH_BREATH_MS      = 3200.0f;   // one breath of the ring
static constexpr int   GH_COMET_STEPS    = 6;         // tail segments, head first
static constexpr float GH_COMET_STEP_DEG = 9.0f;      // length of one segment

// An RGB565 colour scaled to level/255 of its brightness.
static uint16_t dim565(uint16_t c, uint8_t level) {
  const uint16_t r = ((c >> 11) & 0x1F) * level / 255;
  const uint16_t g = ((c >> 5) & 0x3F) * level / 255;
  const uint16_t b = (c & 0x1F) * level / 255;
  return (r << 11) | (g << 5) | b;
}

// The GitHub page's second view (tap to switch): 26 weeks of contributions,
// a column a week, Sunday on top, like the calendar on github.com.
uint8_t githubSub = 0;   // 0 = streak view, 1 = heatmap

static void drawGithubHeat(const GithubData &g) {
  // ponytail: fixed steps, not GitHub's quartiles of the user's own year.
  static const uint8_t STEP_FROM[] = {1, 3, 6, 10};
  static const uint8_t STEP_LEVEL[] = {80, 130, 190, 255};
  const int pitch = 12, x0 = (SCREEN_W - GH_HEAT_WEEKS * pitch) / 2, y0 = 32;
  long total = 0;
  for (int i = 0; i < g.heatDays; i++) {
    total += g.heat[i];
    uint16_t color = rgb(30, 34, 40);
    for (int s = 0; s < 4; s++)
      if (g.heat[i] >= STEP_FROM[s]) color = dim565(COL_GHNUM, STEP_LEVEL[s]);
    gfx->fillRect(x0 + (i / 7) * pitch, y0 + (i % 7) * pitch, pitch - 2, pitch - 2, color);
  }
  char buf[32];
  gfx->setTextSize(2);
  gfx->setTextColor(COL_VALUE);
  snprintf(buf, sizeof(buf), "%ld IN %d WEEKS", total, GH_HEAT_WEEKS);
  gfx->setCursor(x0, 8);  gfx->print(buf);
  gfx->setTextColor(COL_GHNUM);
  snprintf(buf, sizeof(buf), "STREAK %d", g.streak);
  gfx->setCursor(x0, 124);  gfx->print(buf);
  gfx->setTextColor(COL_LABEL);
  snprintf(buf, sizeof(buf), "TODAY %d", g.today);
  gfx->setCursor(SCREEN_W - x0 - (int)strlen(buf) * 12, 124);  gfx->print(buf);
}

void drawGithub() {
  gfx->fillScreen(BG);
  if (!githubConfigured()) {
    centeredText("NO GITHUB USER", 60, 3);
    centeredText("SET GITHUB USER", 100, 2);
    drawPageDots(); return;
  }
  if (drawWifiGate()) return;
  const GithubData &g = netView.github;
  if (!g.valid) { centeredText("UPDATING", 74, 3); drawPageDots(); return; }
  if (githubSub && g.haveActivity) { drawGithubHeat(g); drawPageDots(); return; }

  // Profile picture on the left inside an animated ring. The whole canvas is
  // redrawn every frame, so the animation is just a function of millis().
  const int cx = 60, cy = 60, half = GH_AVATAR_SIZE / 2;
  gfx->draw16bitRGBBitmap(cx - half, cy - half, (uint16_t *)gh_avatar, GH_AVATAR_SIZE, GH_AVATAR_SIZE);
  const bool doneToday = g.haveActivity && g.today > 0;
  const uint32_t ms = millis();
  // The ring breathes while today's contribution is in, and sits dim until then.
  const float breath = 0.5f + 0.5f * sinf(ms * (2.0f * PI / GH_BREATH_MS));
  const uint8_t ringLevel = doneToday ? (uint8_t)(110 + 145 * breath) : 70;
  const uint16_t ringColor = doneToday ? COL_GHNUM : COL_LABEL;
  gfx->fillArc(cx, cy, half + 6, half + 3, 0, 360, dim565(ringColor, ringLevel));
  // A comet runs round it: a bright head and a fading tail.
  const float head = fmodf(ms * (360.0f / GH_ORBIT_MS), 360.0f);
  for (int i = GH_COMET_STEPS - 1; i >= 0; i--) {
    float a0 = head - (i + 1) * GH_COMET_STEP_DEG, a1 = head - i * GH_COMET_STEP_DEG;
    if (a0 < 0) { a0 += 360.0f; a1 += 360.0f; }
    const uint16_t c = dim565(COL_GHNUM, (uint8_t)(255 - i * (200 / GH_COMET_STEPS)));
    // fillArc takes angles inside one turn, so a segment across 360 is drawn in two parts.
    if (a1 > 360.0f) {
      gfx->fillArc(cx, cy, half + 8, half + 1, a0, 360, c);
      gfx->fillArc(cx, cy, half + 8, half + 1, 0, a1 - 360.0f, c);
    } else {
      gfx->fillArc(cx, cy, half + 8, half + 1, a0, a1, c);
    }
  }

  // Streak on the right, today's count under it.
  const int rx = 126;
  char buf[16];
  if (g.haveActivity) snprintf(buf, sizeof(buf), "%d", g.streak);
  else                strlcpy(buf, "--", sizeof(buf));
  gfx->setTextColor(COL_GHNUM);
  gfx->setTextSize(6);
  gfx->setCursor(rx, 10);  gfx->print(buf);
  gfx->setTextColor(COL_LABEL);
  gfx->setTextSize(2);
  gfx->setCursor(rx, 64);  gfx->print("DAY STREAK");
  if (g.haveActivity) {
    if (doneToday)         snprintf(buf, sizeof(buf), "%d TODAY", g.today);
    else if (g.streak > 0) strlcpy(buf, "KEEP IT ALIVE", sizeof(buf));
    else                   strlcpy(buf, "0 TODAY", sizeof(buf));
    gfx->setTextColor(doneToday ? COL_PRICE_UP : COL_VALUE);
    gfx->setCursor(rx, 88);  gfx->print(buf);
  }

  // Bottom row: stars, followers, repos.
  struct { const char *label; long value; bool known; } cells[3] = {
    {"STARS",     g.stars,     g.haveStars},
    {"FOLLOWERS", g.followers, true},
    {"REPOS",     g.repos,     true},
  };
  for (int i = 0; i < 3; i++) {
    const int mid = SCREEN_W / 6 + i * (SCREEN_W / 3);
    if (cells[i].known) formatCount(cells[i].value, buf, sizeof(buf));
    else                strlcpy(buf, "--", sizeof(buf));
    gfx->setTextColor(COL_VALUE);
    gfx->setTextSize(3);
    gfx->setCursor(mid - (int)strlen(buf) * 9, 118);  gfx->print(buf);
    gfx->setTextColor(COL_LABEL);
    gfx->setTextSize(1);
    gfx->setCursor(mid - (int)strlen(cells[i].label) * 3, 145);  gfx->print(cells[i].label);
  }
  drawPageDots();
}

// ── AbdA page ─────────────────────────────────────────────
// Eight animations of the word "AbdA", played one after another. The whole
// canvas is redrawn every frame, so each scene is a function of its own time.

// Length of each scene, in play order. Tune to taste.
static const uint16_t ABDA_SCENE_MS[] = {5200, 5600, 6900, 7000, 6000, 7000, 7000, 6000};
static const int ABDA_CELL = 12, ABDA_X = 22, ABDA_Y = 44;   // big block word, centred

// 5x7 letters, one byte a row, bit 4 = left column.
static const uint8_t ABDA_GLYPHS[4][7] = {
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},   // A
  {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},   // b
  {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F},   // d
  {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},   // A
};

// The word is 23 columns by 7 rows: four letters with a blank column between.
static bool abdaCell(int col, int row) {
  if (col < 0 || col > 22 || row < 0 || row > 6 || col % 6 == 5) return false;
  return (ABDA_GLYPHS[col / 6][row] >> (4 - col % 6)) & 1;
}

// Calls f(col, row, n) for every block of the word; n counts the blocks.
template <typename F> static void abdaEach(F f) {
  int n = 0;
  for (int row = 0; row < 7; row++)
    for (int col = 0; col < 23; col++)
      if (abdaCell(col, row)) f(col, row, n++);
}

// A repeatable pseudo-random number in [0, 1) for n.
static float abdaHash(uint32_t n) {
  n ^= n >> 16; n *= 0x7feb352dUL; n ^= n >> 15; n *= 0x846ca68bUL; n ^= n >> 16;
  return (n & 0xFFFFFF) / 16777216.0f;
}

static uint16_t hsl565(float h, float s, float l) {
  h = fmodf(h, 360.0f);
  if (h < 0) h += 360.0f;
  const float c = (1 - fabsf(2 * l - 1)) * s;
  const float x = c * (1 - fabsf(fmodf(h / 60.0f, 2.0f) - 1));
  const float m = l - c / 2;
  float r = 0, g = 0, b = 0;
  switch ((int)(h / 60.0f)) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
  }
  return rgb((uint8_t)((r + m) * 255), (uint8_t)((g + m) * 255), (uint8_t)((b + m) * 255));
}

// The pen scene's path: the word as strokes, sampled every 3 px.
static int16_t abdaPen[400][2];
static int     abdaPenCount = 0;

static void abdaPenPoint(float letter, float x, float y) {
  if (abdaPenCount >= 400) return;
  abdaPen[abdaPenCount][0] = (int16_t)(ABDA_X + (letter * 6 + x) * ABDA_CELL);
  abdaPen[abdaPenCount][1] = (int16_t)(ABDA_Y + 2 + y * ABDA_CELL);
  abdaPenCount++;
}
static void abdaPenLine(int letter, float x0, float y0, float x1, float y1) {
  const int n = (int)ceilf(hypotf(x1 - x0, y1 - y0) * ABDA_CELL / 3.0f);
  for (int i = 0; i <= n; i++) abdaPenPoint(letter, x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
}
static void abdaPenRing(int letter, float cx, float cy, float r, float start, float dir) {
  const int n = (int)ceilf(2 * PI * r * ABDA_CELL / 3.0f);
  for (int i = 0; i <= n; i++) {
    const float a = start + dir * 2 * PI * i / n;
    abdaPenPoint(letter, cx + r * cosf(a), cy + r * sinf(a));
  }
}
static void abdaPenBigA(int letter) {
  abdaPenLine(letter, 0, 7, 2.5f, 0);
  abdaPenLine(letter, 2.5f, 0, 5, 7);
  abdaPenLine(letter, 1.1f, 4.6f, 3.9f, 4.6f);
}
static void abdaBuildPen() {
  if (abdaPenCount) return;
  abdaPenBigA(0);
  abdaPenLine(1, 0.2f, 0, 0.2f, 7);  abdaPenRing(1, 2.5f, 4.7f, 2.3f, PI, 1);
  abdaPenRing(2, 2.5f, 4.7f, 2.3f, 0, -1);  abdaPenLine(2, 4.8f, 0, 4.8f, 7);
  abdaPenBigA(3);
}

// A: a glowing pen writes the word, the word breathes, then wipes away.
static void abdaScenePen(uint32_t t) {
  abdaBuildPen();
  int from = 0, to = abdaPenCount;
  float level = 1;
  bool pen = false;
  if (t < 2400)      { to = (int)(abdaPenCount * t / 2400); pen = true; }
  else if (t < 4400) level = 0.75f + 0.25f * cosf((t - 2400) * (2 * PI / 2000.0f));
  else if (t < 5000) from = (int)(abdaPenCount * (t - 4400) / 600);
  else               to = 0;
  const uint16_t glow = rgb((uint8_t)(20 * level), (uint8_t)(90 * level), (uint8_t)(45 * level));
  const uint16_t core = rgb((uint8_t)(63 * level), (uint8_t)(215 * level), (uint8_t)(100 * level));
  for (int i = from; i < to; i++) gfx->fillCircle(abdaPen[i][0], abdaPen[i][1], 6, glow);
  for (int i = from; i < to; i++) gfx->fillCircle(abdaPen[i][0], abdaPen[i][1], 3, core);
  if (pen && to > 0) gfx->fillCircle(abdaPen[to - 1][0], abdaPen[to - 1][1], 5, FG);
}

// B: blocks fall into place, a shine sweeps across, they fall out.
static void abdaSceneBlocks(uint32_t t) {
  const float sweep = ((float)t - 2200) / 900 * 34 - 6;   // the shine's column
  abdaEach([&](int col, int row, int n) {
    const int ty = ABDA_Y + row * ABDA_CELL;
    float y = ty;
    if (t < 4300) {
      float p = ((float)t - (col * 45 + abdaHash(n) * 250)) / 520;
      if (p <= 0) return;
      if (p > 1) p = 1;
      y = -20 + (ty + 20) * (1 - (1 - p) * (1 - p));
    } else {
      float p = ((float)t - 4300 - (22 - col) * 25 - abdaHash(n + 99) * 200) / 600;
      if (p < 0) p = 0;
      y = ty + p * p * 220;
      if (y > SCREEN_H) return;
    }
    const bool lit = fabsf(col + row * 0.6f - sweep) < 1.6f;
    gfx->fillRect(ABDA_X + col * ABDA_CELL, (int)y, ABDA_CELL - 2, ABDA_CELL - 2,
                  lit ? FG : hsl565(150 + col * 6, 0.85f, 0.58f));
  });
}

// C: red and cyan copies under white, torn sideways in bursts.
static void abdaSceneGlitch(uint32_t t) {
  const bool burst = t % 2300 > 1900;
  const uint32_t step = t / 60;
  const int split = burst ? 3 + (int)(abdaHash(step) * 5) : 1;
  int tear[7];
  for (int r = 0; r < 7; r++)
    tear[r] = burst && abdaHash(step * 7 + r) > 0.55f ? (int)((abdaHash(step * 13 + r) - 0.5f) * 36) : 0;
  const struct { int dx; uint16_t color; } layers[3] = {
    {-split, rgb(255, 42, 77)}, {split, rgb(0, 229, 255)}, {0, FG}};
  for (const auto &layer : layers)
    abdaEach([&](int col, int row, int) {
      gfx->fillRect(ABDA_X + col * ABDA_CELL + layer.dx + tear[row], ABDA_Y + row * ABDA_CELL,
                    ABDA_CELL - 1, ABDA_CELL - 1, layer.color);
    });
  for (int y = 0; y < SCREEN_H; y += 4) gfx->drawFastHLine(0, y, SCREEN_W, BG);
  if (burst) gfx->fillRect(0, (int)(abdaHash(step * 3) * SCREEN_H), SCREEN_W, 2, rgb(0, 229, 255));
}

// D: digits rain down a 12 px grid; the word flares as each drop passes.
static void abdaSceneRain(uint32_t t) {
  gfx->setTextSize(1);
  for (int c = 0; c < 27; c++) {
    const int head = (int)fmodf(t / (55 + abdaHash(c + 5) * 70) + abdaHash(c) * 40, 32);
    for (int r = 0; r < 15; r++) {
      const int d = head - r;
      const float trail = d >= 0 && d < 10 ? 1 - d / 10.0f : 0;
      if (abdaCell(c - 2, r - 4)) {
        const float v = 0.45f + 0.55f * trail;
        gfx->fillRect(c * 12, r * 12, 11, 11,
                      d == 0 ? FG : rgb((uint8_t)(60 * v), (uint8_t)(255 * v), (uint8_t)(110 * v)));
      } else if (trail > 0) {
        gfx->setTextColor(d == 0 ? rgb(207, 255, 220) : rgb(0, (uint8_t)(190 * trail), (uint8_t)(70 * trail)));
        gfx->setCursor(c * 12 + 3, r * 12 + 2);
        gfx->print(abdaHash(c * 31 + r * 17 + t / 300) > 0.5f ? '1' : '0');
      }
    }
  }
}

// E: the letters ride a wave while colour flows through them.
static void abdaSceneWave(uint32_t t) {
  abdaEach([&](int col, int row, int) {
    gfx->fillRect(ABDA_X + col * ABDA_CELL,
                  ABDA_Y + row * ABDA_CELL + (int)lroundf(sinf(t / 380.0f + col * 0.32f) * 12),
                  ABDA_CELL - 2, ABDA_CELL - 2, hsl565(t / 18.0f + col * 9 + row * 14, 0.9f, 0.6f));
  });
}

// F: striped sun, a grid rolling toward the viewer, the word in chrome.
static void abdaSceneSynth(uint32_t t) {
  const int HORIZON = 112, SUN_Y = 100, SUN_R = 50;
  for (int y = 0; y < HORIZON; y += 4) gfx->fillRect(0, y, SCREEN_W, 4, rgb(y / 2, 0, 20 + y * 7 / 10));
  for (int y = -SUN_R; y < HORIZON - SUN_Y; y++) {
    if (y > -14 && (y + SUN_R) % 9 < 3) continue;   // gaps in the lower half
    const int half = (int)sqrtf((float)(SUN_R * SUN_R - y * y));
    gfx->drawFastHLine(160 - half, SUN_Y + y, half * 2, hsl565(50 - (y + SUN_R) * 0.75f, 1.0f, 0.58f));
  }
  gfx->fillRect(0, HORIZON, SCREEN_W, SCREEN_H - HORIZON, rgb(10, 0, 24));
  const uint16_t grid = rgb(255, 61, 240);
  const float phase = (t % 900) / 900.0f;
  for (int i = 0; i < 7; i++) {
    const float p = (i + phase) / 7;
    gfx->drawFastHLine(0, HORIZON + (int)(60 * p * p), SCREEN_W, grid);
  }
  for (int k = -8; k <= 8; k++) {
    // From the horizon to the bottom edge, cut short where it leaves the side.
    const float top = 160 + k * 14;
    float x = 160 + k * 70, y = SCREEN_H - 1;
    if (x < 0 || x > SCREEN_W - 1) {
      const float edge = x < 0 ? 0 : SCREEN_W - 1;
      y = HORIZON + (SCREEN_H - 1 - HORIZON) * (edge - top) / (x - top);
      x = edge;
    }
    gfx->drawLine((int)top, HORIZON, (int)x, (int)y, grid);
  }
  const int cell = 10, ox = 45, oy = 26, shine = (int)((t / 60) % 60) - 8;
  abdaEach([&](int col, int row, int) {
    gfx->fillRect(ox + col * cell + 3, oy + row * cell + 3, cell - 1, cell - 1, rgb(18, 0, 42));
  });
  abdaEach([&](int col, int row, int) {
    gfx->fillRect(ox + col * cell, oy + row * cell, cell - 1, cell - 1,
                  abs(col - row - shine) < 1 ? FG : hsl565(190 + row * 20, 1.0f, 0.78f - row * 0.05f));
  });
}

// G: a small word bounces off the edges and changes colour on each hit.
static void abdaSceneBounce(uint32_t t) {
  const int cell = 6, mx = SCREEN_W - 23 * cell, my = SCREEN_H - 7 * cell;
  const float vx = t * 0.055f, vy = t * 0.041f;
  const int x = mx - (int)fabsf(fmodf(vx, 2.0f * mx) - mx), y = my - (int)fabsf(fmodf(vy, 2.0f * my) - my);
  const uint16_t color = hsl565(((int)(vx / mx) + (int)(vy / my)) * 67, 0.95f, 0.6f);
  abdaEach([&](int col, int row, int) {
    gfx->fillRect(x + col * cell, y + row * cell, cell - 1, cell - 1, color);
  });
}

// H: a prompt types whoami, the answer appears a letter at a time.
static void abdaSceneTerminal(uint32_t t) {
  const uint16_t green = rgb(63, 185, 80);
  char prompt[48], line[64];
  snprintf(prompt, sizeof(prompt), "%s@github:~$ ", githubConfigured() ? settings.githubUser : "abda");
  int typed = t < 300 ? 0 : (int)((t - 300) / 45);
  if (typed > 6) typed = 6;
  snprintf(line, sizeof(line), "%s%.*s", prompt, typed, "whoami");
  gfx->setTextSize(1);
  gfx->setTextColor(green);
  gfx->setCursor(12, 12);  gfx->print(line);
  const int cell = 10, ox = 12, oy = 52;
  int letters = t < 1200 ? 0 : (int)((t - 1200) / 260) + 1;
  if (letters > 4) letters = 4;
  abdaEach([&](int col, int row, int) {
    if (col < letters * 6) gfx->fillRect(ox + col * cell, oy + row * cell, cell - 1, cell - 1, rgb(230, 255, 233));
  });
  if ((t / 400) % 2 == 0) gfx->fillRect(ox + letters * 6 * cell, oy, 5 * cell - 1, 7 * cell - 1, green);
  if (t > 2600) { gfx->setCursor(12, 140);  gfx->print(prompt); }
}

void drawAbda() {
  static void (*const scenes[])(uint32_t) = {
    abdaScenePen, abdaSceneBlocks, abdaSceneGlitch, abdaSceneRain,
    abdaSceneWave, abdaSceneSynth, abdaSceneBounce, abdaSceneTerminal};
  const size_t count = sizeof(scenes) / sizeof(scenes[0]);
  static_assert(sizeof(ABDA_SCENE_MS) / sizeof(ABDA_SCENE_MS[0]) == sizeof(scenes) / sizeof(scenes[0]),
                "every scene has a length");
  uint32_t total = 0;
  for (size_t i = 0; i < count; i++) total += ABDA_SCENE_MS[i];
  uint32_t t = millis() % total;
  size_t scene = 0;
  while (t >= ABDA_SCENE_MS[scene]) t -= ABDA_SCENE_MS[scene++];
  gfx->fillScreen(BG);
  scenes[scene](t);
  drawPageDots();
}

// One counter card on the Odoo page: a small label over a big number.
// Width of `s` in px in the current font.
static int textWidth(const char *s) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// Sets `big`, or `small` when `s` in `big` is wider than maxW px.
static void fitFont(const uint8_t *big, const uint8_t *small, const char *s, int maxW) {
  gfx->setFont(big);
  if (textWidth(s) > maxW) gfx->setFont(small);
}

// ── Text pages: a slim top line and big text ─────────────
static const uint16_t PAGE_BG = 0x20C4;   // rgb(33, 24, 33)

// The top line: `title` on the left in `color`, `right` (the time when null)
// on the right, in a small bold font over 14 px.
static void slimHeader(const char *title, uint16_t color, const char *right) {
  char clock[8];
  if (!right) {
    const LocalNow now = localNow();
    snprintf(clock, sizeof(clock), "%02u:%02u", (unsigned)now.hour, (unsigned)now.min);
    right = clock;
  }
  gfx->setTextSize(1);
  gfx->setFont(u8g2_font_helvB10_tr);
  gfx->setTextColor(color);
  gfx->setCursor(10, 12);
  gfx->print(title);
  gfx->setTextColor(rgb(173, 154, 165));
  gfx->setCursor(SCREEN_W - 10 - textWidth(right), 12);
  gfx->print(right);
}

// The row stripe of each source on the text pages.
static const uint16_t STRIPE_MEETING = rgb(74, 195, 203), STRIPE_HOURS = rgb(203, 166, 189),
                      STRIPE_GITHUB = rgb(200, 200, 210), STRIPE_ODOO = rgb(160, 110, 145),
                      STRIPE_CLAUDE = rgb(217, 119, 87);
static const char *const DAY_NAMES[] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};

// One row of a text page: a card h px high with a stripe on its left. Returns
// the baseline for its text.
static int pageRow(int y, int h, uint16_t stripe) {
  gfx->fillRoundRect(10, y, 300, h, 6, rgb(49, 40, 49));
  gfx->fillRoundRect(16, y + h / 2 - 7, 4, 14, 2, stripe);
  return y + h / 2 + 6;
}

// Prints `text` at the cursor, cut to maxW with ".." when it does not fit.
static void printCut(const char *text, int maxW) {
  char cut[72];
  snprintf(cut, sizeof(cut), "%s", text);
  for (int n = strlen(cut); n > 0 && textWidth(cut) > maxW; n--) strcpy(cut + n - 1, "..");
  gfx->print(cut);
}

// The parts of a row with a dot between them, in the biggest font that fits
// maxW; parts that still do not fit are left out from the end.
static void drawParts(int x, int baseline, int maxW, const char *const *parts, int count, uint16_t color) {
  static const uint8_t *const FONTS[] = {u8g2_font_helvB14_tr, u8g2_font_helvB12_tr};
  const int gap = 15;
  int width = 0;
  for (const uint8_t *font : FONTS) {
    gfx->setFont(font);
    width = (count - 1) * gap;
    for (int i = 0; i < count; i++) width += textWidth(parts[i]);
    if (width <= maxW) break;
  }
  while (count > 1 && width > maxW) width -= textWidth(parts[--count]) + gap;
  gfx->setTextColor(color);
  const int startX = x;
  for (int i = 0; i < count; i++) {
    gfx->setCursor(x, baseline);
    printCut(parts[i], maxW - (x - startX));
    x += textWidth(parts[i]) + gap;
    if (i + 1 < count) gfx->fillCircle(x - gap / 2 - 1, baseline - 5, 2, rgb(173, 154, 165));
  }
}

// Breaks text at its spaces into at most two lines of maxW in `font`; the
// second ends in ".." when the text goes on. Returns the number of lines.
static int wrapToWidth(const char *text, const uint8_t *font, int maxW, char lines[2][72]) {
  gfx->setFont(font);
  int count = 0;
  const char *at = text;
  while (*at && count < 2) {
    while (*at == ' ') at++;
    const int left = strlen(at);
    char probe[72];
    int fit = 0, atSpace = 0;
    for (int n = 1; n <= left && n < 70; n++) {
      memcpy(probe, at, n);
      probe[n] = '\0';
      if (textWidth(probe) > maxW) break;
      fit = n;
      if (at[n] == ' ' || at[n] == '\0') atSpace = n;
    }
    if (count == 1 && fit < left) {   // the last line: cut
      snprintf(lines[1], 72, "%s", at);
      for (int n = strlen(lines[1]); n > 0 && textWidth(lines[1]) > maxW; n--) strcpy(lines[1] + n - 1, "..");
      return 2;
    }
    const int take = fit < left && atSpace > 0 ? atSpace : fit;
    if (take == 0) break;
    memcpy(lines[count], at, take);
    lines[count][take] = '\0';
    count++;
    at += take;
  }
  return count;
}

// One counter card on the Odoo page: a label over a big number. u8g2 fonts:
// the cursor y is the baseline.
static void odooCounter(int x, int top, const char *label, int value, uint16_t color, uint16_t card, uint16_t muted) {
  gfx->fillRoundRect(x, top, 97, 75, 6, card);
  gfx->setFont(u8g2_font_helvB10_tr);
  gfx->setTextColor(muted);
  gfx->setCursor(x + 8, top + 16);
  gfx->print(label);
  char buf[12];
  snprintf(buf, sizeof(buf), "%d", value);
  fitFont(u8g2_font_logisoso38_tn, u8g2_font_logisoso28_tn, buf, 81);
  gfx->setTextColor(color);
  gfx->setCursor(x + 8, top + 66);
  gfx->print(buf);
}

// Odoo page, in Odoo's purple and teal: the trigram and the time on top, a
// ring of today's hours against the working day, the open tasks, the tasks in
// code review, and the task changed last with its stage.
void drawOdoo() {
  const uint16_t teal = rgb(74, 195, 203), card = rgb(49, 40, 49);
  const uint16_t muted = rgb(173, 154, 165), text = rgb(247, 243, 247);
  char buf[48];
  gfx->fillScreen(PAGE_BG);
  slimHeader(ODOO_TRIGRAM, teal, nullptr);
  const OdooData &d = netView.odoo;
  if (!d.valid) {
    gfx->setFont((const GFXfont *)nullptr);
    centeredTextColor("LOADING ODOO", 78, 2, muted);
    return;
  }

  // From here on u8g2 fonts at size 1; the cursor y is the baseline.
  const int cx = 47, cy = 56, r = 37;
  const float day = d.hours / ODOO_DAY_HOURS;
  gfx->fillArc(cx, cy, r, r - 6, 0, 360, card);
  if (day > 0) gfx->fillArc(cx, cy, r, r - 6, 270, 270 + 360 * (day > 1 ? 1 : day), teal);
  odooHours(d.hours, buf, sizeof(buf));
  fitFont(u8g2_font_logisoso22_tn, u8g2_font_logisoso16_tn, buf, 58);   // inside the ring
  gfx->setTextColor(text);
  gfx->setCursor(cx - textWidth(buf) / 2, cy + 6);
  gfx->print(buf);
  snprintf(buf, sizeof(buf), "of %dh", (int)ODOO_DAY_HOURS);
  gfx->setFont(u8g2_font_helvB10_tr);
  gfx->setTextColor(muted);
  gfx->setCursor(cx - textWidth(buf) / 2, cy + 21);
  gfx->print(buf);

  odooCounter(100, 18, "OPEN TASKS", d.open, text, card, muted);
  odooCounter(213, 18, "IN REVIEW", d.review, teal, card, muted);

  // One of the tasks changed last, its name over two lines; a tap shows the next, "2/6" says which.
  gfx->fillRoundRect(10, 98, 300, 71, 6, card);
  if (d.taskCount > 0) {
    const OdooTask &task = d.tasks[odooSub % d.taskCount];
    char lines[2][72];
    const int count = wrapToWidth(task.name, u8g2_font_helvB14_tr, 280, lines);
    gfx->setTextColor(text);
    for (int i = 0; i < count; i++) {
      gfx->setCursor(20, 118 + i * 20);
      gfx->print(lines[i]);
    }
    gfx->setFont(u8g2_font_helvB10_tr);
    snprintf(buf, sizeof(buf), "#%d  %u/%u", task.id, (unsigned)(odooSub % d.taskCount) + 1, (unsigned)d.taskCount);
    gfx->setTextColor(muted);
    gfx->setCursor(300 - textWidth(buf), 162);
    gfx->print(buf);
    gfx->setTextColor(teal);
    gfx->setCursor(20, 162);
    printCut(task.stage, 270 - textWidth(buf) - 10);
  }
  gfx->setFont((const GFXfont *)nullptr);   // back to the 6x8 font the other pages use
}

// Pull requests page: the PRs updated last, each with a dot for its checks
// (teal passed, red failed, amber running, gray none).
void drawPrs() {
  const uint16_t card = rgb(49, 40, 49), text = rgb(247, 243, 247), muted = rgb(173, 154, 165);
  const uint16_t dots[] = {rgb(120, 110, 118), rgb(74, 195, 203), rgb(255, 80, 90), rgb(247, 162, 49)};
  static const uint8_t *const FONTS[] = {u8g2_font_helvB14_tr, u8g2_font_helvB12_tr, u8g2_font_helvB10_tr};
  char buf[32];
  gfx->fillScreen(PAGE_BG);
  if (pc.reviews > 0) snprintf(buf, sizeof(buf), "%d open  %d to review", pc.prs, pc.reviews);
  else                snprintf(buf, sizeof(buf), "%d open", pc.prs);
  slimHeader("PULL REQUESTS", muted, buf);
  // Up to three PRs get two lines each; four get one line each.
  const bool tall = pc.prCount <= 3;
  const int h = tall ? 48 : 36;
  for (uint8_t i = 0; i < pc.prCount; i++) {
    const int y = 17 + i * (h + 3), mid = y + h / 2;
    const PcPr &pr = pc.prList[i];
    gfx->fillRoundRect(10, y, 300, h, 6, card);
    gfx->fillCircle(24, mid, 5, dots[pr.check < 4 ? pr.check : 0]);
    gfx->setTextColor(text);
    char lines[2][72];
    gfx->setFont(u8g2_font_helvB14_tr);
    if (tall && textWidth(pr.title) > 244) {
      const int count = wrapToWidth(pr.title, u8g2_font_helvB12_tr, 244, lines);
      for (int line = 0; line < count; line++) {
        gfx->setCursor(36, mid - (count - 1) * 9 + 5 + line * 18);
        gfx->print(lines[line]);
      }
    } else {
      for (const uint8_t *font : FONTS) {   // the biggest font the title fits in
        gfx->setFont(font);
        if (textWidth(pr.title) <= 244) break;
      }
      gfx->setCursor(36, mid + 6);
      printCut(pr.title, 244);
    }
    if (pr.review == PR_REVIEW_CHANGES) {          // a reviewer asked for changes: "!"
      gfx->fillRoundRect(286, mid - 8, 16, 16, 4, dots[PR_CHECK_FAILED]);
      gfx->fillRect(293, mid - 5, 2, 7, text);
      gfx->fillRect(293, mid + 4, 2, 2, text);
    } else if (pr.review == PR_REVIEW_APPROVED) {   // approved: a tick
      gfx->fillRoundRect(286, mid - 8, 16, 16, 4, dots[PR_CHECK_OK]);
      gfx->drawLine(289, mid, 292, mid + 3, card);
      gfx->drawLine(292, mid + 3, 298, mid - 4, card);
    }
  }
  gfx->setFont((const GFXfont *)nullptr);
}

// ── The day's totals ─────────────────────────────────────
static const char *const DAY_PATH = "/day.json";

void daySave() {
  JsonDocument doc;
  doc["date"]     = day.date;
  doc["focus"]    = day.focusS;
  doc["meet"]     = day.meetS;
  doc["sessions"] = day.sessions;
  doc["best"]     = day.best;
  doc["briefed"]  = day.briefed;
  writeJsonAtomic(DAY_PATH, doc);
}

static void dayLoad() {
  File file = LittleFS.open(DAY_PATH);
  JsonDocument doc;
  if (!file || deserializeJson(doc, file)) return;
  day.date     = doc["date"] | 0;
  day.focusS   = doc["focus"] | 0;
  day.meetS    = doc["meet"] | 0;
  day.sessions = doc["sessions"] | 0;
  day.best     = doc["best"] | 0;
  day.briefed  = doc["briefed"] | false;
}

// A new date clears the counts.
void dayRoll() {
  if (!clockTrusted) return;
  const LocalNow now = localNow();
  const int32_t date = now.year * 10000 + now.month * 100 + now.mday;
  if (date != day.date) day = {date, 0, 0, 0, day.best, false};
}

// ── Today ────────────────────────────────────────────────
void todayShow(const char *title, uint32_t ms) {
  todayTitle = title;
  todayUntil = (millis() + ms) | 1;
  yesterdayUntil = 0;
}

void drawToday() {
  const uint16_t muted = rgb(173, 154, 165), text = rgb(247, 243, 247);
  const uint16_t STRIPES[] = {STRIPE_MEETING, STRIPE_HOURS, STRIPE_GITHUB, STRIPE_ODOO};
  const OdooData &d = netView.odoo;
  char next[56], logged[16], focusText[12], meetText[12], reviews[20], changes[20], open[16], due[16], tasks[16];
  char inText[12], outText[12];
  gfx->fillScreen(PAGE_BG);
  slimHeader(todayTitle, strcmp(todayTitle, "TODAY") == 0 ? muted : rgb(74, 195, 203), nullptr);

  const int64_t utc = (int64_t)time(nullptr);
  const LocalNow today = localNow();
  snprintf(next, sizeof(next), "%s", d.valid ? "No more meetings today" : "Waiting for Odoo");
  for (uint8_t i = 0; i < d.meetingCount; i++) {
    if (d.meetings[i].start < utc - 300) continue;   // under way for 5 minutes: no longer next
    const time_t start = (time_t)d.meetings[i].start;
    struct tm local;
    localtime_r(&start, &local);
    if (local.tm_mday != today.mday || local.tm_mon + 1 != today.month) break;   // another day's: not on the Today page
    snprintf(next, sizeof(next), "%02d:%02d  %s", local.tm_hour, local.tm_min, d.meetings[i].name);
    break;
  }
  const uint32_t focus = day.focusS + (timerStartMs ? secondsSince(timerStartMs) : 0);
  const uint32_t meet  = day.meetS + (micSinceMs ? secondsSince(micSinceMs) : 0);
  odooHours(d.hours, logged, sizeof(logged));
  strcat(logged, " logged");
  snprintf(focusText, sizeof(focusText), "%lu:%02lu focus", (unsigned long)(focus / 3600), (unsigned long)(focus / 60 % 60));
  snprintf(meetText, sizeof(meetText), "%lu:%02lu meet", (unsigned long)(meet / 3600), (unsigned long)(meet / 60 % 60));
  snprintf(reviews, sizeof(reviews), "%d to review", pc.reviews);
  snprintf(changes, sizeof(changes), "%d changes", pc.changes);
  snprintf(open, sizeof(open), "%d open", pc.prs);
  snprintf(due, sizeof(due), "%d due", d.activities);
  snprintf(tasks, sizeof(tasks), "%d tasks", d.open);
  snprintf(inText, sizeof(inText), "in %s", pc.in);
  snprintf(outText, sizeof(outText), "out %s", pc.out);

  for (uint8_t row = 0; row < 4; row++) {
    const int baseline = pageRow(16 + row * 39, 36, STRIPES[row]);
    if (row == 0) {
      const char *parts[] = {next};
      drawParts(28, baseline, 274, parts, 1, text);
    } else if (row == 1) {   // when I came in, on the evening recap also when I left, then the hours
      const char *parts[5];
      int count = 0;
      if (pc.known && pc.in[0]) parts[count++] = inText;
      if (pc.known && pc.out[0] && strcmp(todayTitle, "DAY DONE") == 0) parts[count++] = outText;
      parts[count++] = d.valid ? logged : "-:-- logged";
      parts[count++] = focusText;
      if (meet) parts[count++] = meetText;
      drawParts(28, baseline, 274, parts, count, text);
    } else if (row == 2) {
      const char *parts[] = {reviews, changes, open};
      if (pc.known) drawParts(28, baseline, 274, parts, 3, text);
      else { const char *away[] = {"Laptop out of reach"}; drawParts(28, baseline, 274, away, 1, muted); }
    } else {
      const char *parts[] = {due, tasks};
      if (d.valid) drawParts(28, baseline, 274, parts, 2, text);
      else { const char *wait[] = {"Waiting for Odoo"}; drawParts(28, baseline, 274, wait, 1, muted); }
    }
  }
  gfx->setFont((const GFXfont *)nullptr);
}

// The working day before, ahead of the day's first Today page: when I came in
// and left, the hours logged, my tasks changed that day, and the pull requests
// merged and reviewed.
void drawYesterday() {
  const uint16_t muted = rgb(173, 154, 165), text = rgb(247, 243, 247);
  const OdooData &d = netView.odoo;
  char logged[16], merged[16], reviewed[16], span[16] = "";
  gfx->fillScreen(PAGE_BG);
  slimHeader("YESTERDAY", rgb(74, 195, 203), d.valid ? DAY_NAMES[d.beforeWday] : nullptr);
  odooHours(d.hoursBefore, logged, sizeof(logged));
  strcat(logged, " logged");
  snprintf(merged, sizeof(merged), "%d merged", pc.merged);
  snprintf(reviewed, sizeof(reviewed), "%d reviewed", pc.reviewed);
  if (pc.known && pc.yin[0] && pc.yout[0]) snprintf(span, sizeof(span), "%s - %s", pc.yin, pc.yout);
  else if (pc.known && pc.yin[0])          snprintf(span, sizeof(span), "in %s", pc.yin);
  int y = 16;
  const char *hours[] = {span, d.valid ? logged : "Waiting for Odoo"};
  const bool times = span[0];
  drawParts(28, pageRow(y, 28, STRIPE_HOURS), 274, hours + !times, 1 + times, d.valid || times ? text : muted);
  for (uint8_t i = 0; d.valid && i < max<uint8_t>(d.beforeTaskCount, 1); i++) {
    const char *task[] = {d.beforeTaskCount ? d.beforeTasks[i] : "No task changed"};
    y += 31;
    drawParts(28, pageRow(y, 28, STRIPE_ODOO), 274, task, 1, d.beforeTaskCount ? text : muted);
  }
  y += 31;
  const char *prs[] = {merged, reviewed}, *away[] = {"Laptop out of reach"};
  drawParts(28, pageRow(y, 28, STRIPE_GITHUB), 274, pc.known ? prs : away, pc.known ? 2 : 1, pc.known ? text : muted);
  gfx->setFont((const GFXfont *)nullptr);
}

// A bar of `percent`, w px wide: red from 80 %, amber from 50 %, else teal.
static void usageBar(int x, int y, int w, uint8_t percent, uint16_t back) {
  const uint16_t fill = percent >= 80 ? rgb(255, 80, 90) : percent >= 50 ? rgb(247, 162, 49) : rgb(74, 195, 203);
  gfx->fillRoundRect(x, y, w, 8, 3, back);
  if (percent) gfx->fillRoundRect(x, y, max(6, w * min<int>(percent, 100) / 100), 8, 3, fill);
}

// Claude Code: the 5-hour and 7-day limits with when each resets, then the
// open sessions with their state and for how long.
void drawClaude() {
  const uint16_t muted = rgb(173, 154, 165), text = rgb(247, 243, 247);
  static const char *const LIMITS[] = {"5h", "7d"};
  static const char *const STATES[] = {"working", "waiting for you", "done"};
  const uint16_t COLORS[] = {rgb(74, 195, 203), rgb(247, 162, 49), muted};
  gfx->fillScreen(PAGE_BG);
  slimHeader("CLAUDE", STRIPE_CLAUDE, nullptr);
  if (!pc.known) {
    const char *away[] = {"Laptop out of reach"};
    drawParts(28, pageRow(16, 28, STRIPE_CLAUDE), 274, away, 1, muted);
    gfx->setFont((const GFXfont *)nullptr);
    return;
  }
  const uint8_t rows = (pc.useKnown ? 2 : 1) + max<uint8_t>(pc.sessionCount, 1);
  const int pitch = rows > 5 ? 26 : 31;   // six rows fit only closer together
  const uint8_t used[] = {pc.use5h, pc.use7d};
  int y = 16;
  if (!pc.useKnown) {
    const char *none[] = {"No figures yet"};
    drawParts(28, pageRow(y, pitch - 3, STRIPE_CLAUDE), 274, none, 1, muted);
    y += pitch;
  }
  for (int i = 0; pc.useKnown && i < 2; i++, y += pitch) {
    const int baseline = pageRow(y, pitch - 3, STRIPE_CLAUDE);
    char percent[8], reset[16];
    snprintf(percent, sizeof(percent), "%u%%", (unsigned)used[i]);
    gfx->setFont(u8g2_font_helvB14_tr);
    gfx->setTextColor(text);
    gfx->setCursor(28, baseline);
    gfx->print(LIMITS[i]);
    usageBar(56, baseline - 9, 86, used[i], PAGE_BG);
    gfx->setCursor(150, baseline);
    gfx->print(percent);
    if (pc.useReset[i][0]) {
      snprintf(reset, sizeof(reset), "resets %s", pc.useReset[i]);
      gfx->setFont(u8g2_font_helvB12_tr);
      gfx->setTextColor(muted);
      gfx->setCursor(302 - textWidth(reset), baseline);
      gfx->print(reset);
    }
  }
  if (!pc.sessionCount) {
    const char *none[] = {"No open sessions"};
    drawParts(28, pageRow(y, pitch - 3, muted), 274, none, 1, muted);
  }
  for (uint8_t i = 0; i < pc.sessionCount; i++, y += pitch) {
    const PcSession &session = pc.sessions[i];
    const uint8_t state = session.state < 3 ? session.state : 0;
    const int baseline = pageRow(y, pitch - 3, COLORS[state]);
    char span[12] = "", right[32];
    if (clockTrusted && session.since > 0) formatSpan((uint32_t)max<int64_t>((int64_t)time(nullptr) - session.since, 0), span, sizeof(span));
    snprintf(right, sizeof(right), span[0] ? "%s %s" : "%s", STATES[state], span);
    gfx->setFont(u8g2_font_helvB12_tr);
    const int rightW = textWidth(right), nameW = 264 - rightW;
    gfx->setTextColor(COLORS[state]);
    gfx->setCursor(302 - rightW, baseline);
    gfx->print(right);
    fitFont(u8g2_font_helvB14_tr, u8g2_font_helvB12_tr, session.project, nameW);
    gfx->setTextColor(text);
    gfx->setCursor(28, baseline);
    printCut(session.project, nameW);
  }
  gfx->setFont((const GFXfont *)nullptr);
}

// ── A line from the PC (buddy-say) ───────────────────────
static constexpr uint32_t NOTICE_MS = 20000UL;

void noticeShow(const char *line) {
  char plain[112];
  odooAscii(line, plain, sizeof(plain), sizeof(plain) - 1);
  noticeCount = odooWrap(plain, &noticeLines[0][0], sizeof(noticeLines[0]) - 1, 4);
  noticeUntil = (millis() + NOTICE_MS) | 1;
}

void drawNotice() {
  gfx->fillScreen(rgb(33, 24, 33));
  gfx->fillRect(0, 0, SCREEN_W, 6, rgb(74, 195, 203));
  const int top = (SCREEN_H - noticeCount * 24) / 2 + 4;
  for (uint8_t i = 0; i < noticeCount; i++) centeredTextColor(noticeLines[i], top + i * 24, 2, rgb(247, 243, 247));
}

// ── Bug Hop ──────────────────────────────────────────────
// A one-tap runner. The Buddy runs; a tap makes it hop over the bugs that crawl
// in from the right, and it stays down for the wasps. The score is the distance
// and the pace picks up with it. The page is reached by swipe only.
#define GAME_GROUND 142
#define GAME_BUGS   3
#define GAME_X      40      // the Buddy's left edge; its hit box is GAME_W wide and GAME_H high
#define GAME_W      32
#define GAME_H      34
enum : uint8_t { GAME_IDLE, GAME_RUN, GAME_OVER };
enum : uint8_t { BUG_SMALL, BUG_BIG, BUG_PAIR, BUG_WASP };
// Each kind's hit box: width, height, and how far above the ground it starts.
static const uint8_t BUG_BOX[][3] = {{22, 14, 0}, {34, 20, 0}, {44, 14, 0}, {26, 12, 44}};
static struct {
  uint8_t phase, kind[GAME_BUGS];
  float height, rise;              // px above the ground, px/s upward
  float speed, travelled;          // px/s and px since the start
  float bugX[GAME_BUGS];
  uint32_t lastMs, phaseAt, landedAt, tapAt;
} game = {};

bool gameRunning() { return game.phase == GAME_RUN; }
static uint32_t gameScore() { return (uint32_t)(game.travelled / 12); }

// Puts bug i to the right of the others, 0.75 to 1.35 s away at this pace: time
// to land and take off again. Bigger bugs and wasps come with the score.
static void gameSpawn(uint8_t i) {
  float last = SCREEN_W;
  for (uint8_t other = 0; other < GAME_BUGS; other++) {
    if (other != i) last = max(last, game.bugX[other] + BUG_BOX[game.kind[other]][0]);
  }
  const uint32_t score = gameScore();
  const long pick = random(0, 100);
  game.bugX[i] = last + game.speed * (0.75f + random(0, 61) / 100.0f);
  game.kind[i] = score > 180 && pick < 22 ? BUG_WASP : score > 120 && pick < 45 ? BUG_PAIR : score > 40 && pick < 70 ? BUG_BIG : BUG_SMALL;
}

void gameTap() {
  const uint32_t nowMs = millis();
  if (game.phase == GAME_RUN) { game.tapAt = nowMs | 1; return; }        // drawGame() makes the hop
  if (game.phase == GAME_OVER && nowMs - game.phaseAt < 700UL) return;   // that tap was for the run that just ended
  game = {};
  game.phase = GAME_RUN;
  game.speed = 150;
  game.lastMs = game.phaseAt = nowMs;
  for (uint8_t i = 0; i < GAME_BUGS; i++) gameSpawn(i);
}

static void gameDrawBug(int x, uint8_t kind, uint32_t nowMs) {
  const uint16_t shell = rgb(255, 80, 90), dark = rgb(150, 40, 50), sky = rgb(33, 24, 33);
  const int step = (nowMs / 100) % 2;
  if (kind == BUG_PAIR) {
    gameDrawBug(x, BUG_SMALL, nowMs);
    gameDrawBug(x + 22, BUG_SMALL, nowMs + 100);
  } else if (kind == BUG_WASP) {
    const int y = GAME_GROUND - 50 + (nowMs / 160) % 2;   // it hovers
    gfx->fillTriangle(x + 10, y - 4, x + 19, y - 4, x + 14 + (step ? 5 : -4), y - 15, rgb(200, 210, 225));   // wings
    gfx->fillEllipse(x + 13, y, 13, 6, rgb(247, 162, 49));
    gfx->fillRect(x + 11, y - 5, 3, 11, sky);
    gfx->fillRect(x + 18, y - 4, 3, 9, sky);
    gfx->fillRect(x + 4, y - 2, 3, 3, sky);                                                                   // eye
    gfx->fillTriangle(x + 25, y - 2, x + 25, y + 2, x + 30, y + 1, rgb(247, 162, 49));                        // sting
  } else {
    const int w = BUG_BOX[kind][0], h = BUG_BOX[kind][1];
    gfx->fillEllipse(x + w / 2 + 2, GAME_GROUND - h / 2 - 2, w / 2 - 2, h / 2, shell);
    gfx->drawFastVLine(x + w / 2 + 3, GAME_GROUND - h - 1, h - 2, dark);                    // between the wing cases
    gfx->fillCircle(x + w / 2 - 3, GAME_GROUND - h / 2 - 4, 1, rgb(247, 243, 247));         // a shine on the shell
    gfx->fillCircle(x + 4, GAME_GROUND - h / 2 - 1, kind == BUG_BIG ? 6 : 4, dark);         // head
    gfx->drawLine(x + 3, GAME_GROUND - h / 2 - 4, x - 3, GAME_GROUND - h - 3 + 2 * step, dark);   // feelers
    gfx->drawLine(x + 5, GAME_GROUND - h / 2 - 5, x + 1, GAME_GROUND - h - 5 + 2 * step, dark);
    for (int leg = 0; leg < 3; leg++) {
      const int at = x + w / 4 + leg * w / 4;
      gfx->fillRect(at - 1 + 2 * ((step + leg) % 2), GAME_GROUND - 3, 2, 3, dark);
    }
  }
}

void drawGame() {
  static const char *const DRIFT[] = {"{ }", "</>", "0101"};
  const uint16_t sky = rgb(33, 24, 33), far = rgb(44, 34, 44), farText = rgb(74, 60, 72), soil = rgb(115, 73, 99), pebble = rgb(70, 50, 66);
  const uint16_t teal = rgb(74, 195, 203), tealDark = rgb(40, 130, 138), text = rgb(247, 243, 247), muted = rgb(173, 154, 165);
  const uint32_t nowMs = millis();
  if (game.phase == GAME_RUN) {
    const float dt = min((nowMs - game.lastMs) / 1000.0f, 0.05f);
    if (game.tapAt && millisSince(game.tapAt) < 130UL && game.height <= 0) {   // a tap just before landing still hops
      game.rise = 500;
      game.tapAt = 0;
    }
    game.height += game.rise * dt;
    game.rise -= 1500 * dt;
    if (game.height <= 0) {
      if (game.rise < -100) game.landedAt = nowMs | 1;
      game.height = game.rise = 0;
    }
    game.travelled += game.speed * dt;
    game.speed = min(150 + game.travelled * 0.012f, 340.0f);
    for (uint8_t i = 0; i < GAME_BUGS; i++) {
      const uint8_t *box = BUG_BOX[game.kind[i]];
      game.bugX[i] -= game.speed * dt;
      if (game.bugX[i] + box[0] < -4) gameSpawn(i);
      // 4 px of mercy on each side, 3 above and below.
      if (game.bugX[i] < GAME_X + GAME_W - 4 && game.bugX[i] + box[0] > GAME_X + 4
          && game.height < box[2] + box[1] - 3 && game.height + GAME_H > box[2] + 3) {
        game.phase = GAME_OVER;
        game.phaseAt = nowMs;
        if (gameScore() > day.best) { day.best = (uint16_t)min<uint32_t>(gameScore(), 65535); daySave(); }
      }
    }
    nextAutoPage = nowMs + PAGE_AUTO_INTERVAL_MS;   // no page flip in the middle of a run
  }
  game.lastMs = nowMs;

  // Far away, blocks of code drift by a fifth as fast; the ground keeps pace.
  gfx->fillScreen(sky);
  gfx->setTextSize(1);
  gfx->setTextColor(farText);
  gfx->setTextWrap(false);   // a block leaving on the right must not send its text to the next line
  for (int i = 0; i < 3; i++) {
    int x = i * 150 + 60 - (int)fmodf(game.travelled * 0.2f, 450.0f);
    if (x < -50) x += 450;
    gfx->fillRoundRect(x, 36 + i * 22 % 40, 44, 14, 4, far);
    gfx->setCursor(x + 22 - (int)strlen(DRIFT[i]) * 3, 39 + i * 22 % 40);
    gfx->print(DRIFT[i]);
  }
  gfx->setTextWrap(true);
  gfx->fillRect(0, GAME_GROUND, SCREEN_W, 2, soil);
  for (int x = -(int)fmodf(game.travelled, 32.0f); x < SCREEN_W; x += 32) {
    gfx->fillRect(x + 6, GAME_GROUND + 4, 9, 2, pebble);
    gfx->fillRect(x + 23, GAME_GROUND + 7, 4, 2, pebble);
  }
  for (uint8_t i = 0; i < GAME_BUGS && game.phase != GAME_IDLE; i++) {
    if (game.bugX[i] < SCREEN_W) gameDrawBug((int)game.bugX[i], game.kind[i], game.phase == GAME_RUN ? nowMs : game.phaseAt);
  }

  // The Buddy: stretched on the way up, squashed as it lands, two feet that take turns.
  const bool air = game.height > 0, over = game.phase == GAME_OVER;
  const uint32_t landed = game.landedAt ? millisSince(game.landedAt) : 1000;
  const int w = air && game.rise > 150 ? 29 : !air && landed < 90 ? 36 : GAME_W;
  const int h = air && game.rise > 150 ? 32 : !air && landed < 90 ? 24 : 28;
  const int stride = game.phase == GAME_RUN && !air ? (int)((nowMs / 85) % 2) : -1;   // the foot that is up; -1: neither
  const int left = GAME_X + (GAME_W - w) / 2, bottom = GAME_GROUND - (int)game.height;
  const int top = bottom - 6 - h + (stride == 1);
  gfx->fillRect(left + 5, bottom - 6, 7, air || stride == 0 ? 3 : 6, tealDark);
  gfx->fillRect(left + w - 12, bottom - 6, 7, air || stride == 1 ? 3 : 6, tealDark);
  gfx->fillRoundRect(left, top, w, h, 8, teal);
  const int eye = left + w - 21;   // the eyes sit forward: it looks where it runs
  if (over) {
    for (int k = 0; k < 2; k++) {
      gfx->drawLine(eye + k * 11, top + 8, eye + k * 11 + 6, top + 15, sky);
      gfx->drawLine(eye + k * 11 + 6, top + 8, eye + k * 11, top + 15, sky);
      gfx->drawLine(eye + k * 11 + 1, top + 8, eye + k * 11 + 7, top + 15, sky);
      gfx->drawLine(eye + k * 11 + 7, top + 8, eye + k * 11 + 1, top + 15, sky);
    }
  } else if (nowMs % 3100 < 110) {   // a blink
    gfx->fillRect(eye, top + 12, 6, 2, sky);
    gfx->fillRect(eye + 11, top + 12, 6, 2, sky);
  } else {
    gfx->fillRoundRect(eye, top + 7, 6, 10, 2, sky);
    gfx->fillRoundRect(eye + 11, top + 7, 6, 10, 2, sky);
    gfx->fillRect(eye + 3, top + 9, 2, 3, RGB565_WHITE);   // a glint in each
    gfx->fillRect(eye + 14, top + 9, 2, 3, RGB565_WHITE);
  }
  for (int puff = 0; puff < 3 && !air && landed < 170; puff++) {   // dust where it landed
    gfx->fillRect(GAME_X - 3 - puff * 7 - (int)landed / 12, GAME_GROUND - 4 - (puff % 2) * 4, 3, 3, pebble);
  }

  char buf[16];
  const uint32_t score = gameScore();
  gfx->setTextSize(2);
  gfx->setTextColor(game.phase == GAME_RUN && score >= 100 && score % 100 < 6 ? teal : text);   // every 100
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  gfx->setCursor(SCREEN_W - 10 - (int)strlen(buf) * 12, 8);
  gfx->print(buf);
  gfx->setTextSize(1);
  gfx->setTextColor(muted);
  snprintf(buf, sizeof(buf), "BEST %u", (unsigned)day.best);
  gfx->setCursor(10, 12);
  gfx->print(buf);
  if (game.phase == GAME_IDLE) {
    centeredTextColor("BUG HOP", 44, 4, text);
    centeredTextColor("TAP TO PLAY", 90, 2, muted);
  } else if (over) {
    centeredTextColor("SPLAT", 44, 4, rgb(255, 80, 90));
    if (nowMs - game.phaseAt >= 700UL) centeredTextColor("TAP TO RETRY", 90, 2, muted);
  }
}

// A whole frame takes 30 ms to send to the screen. During a run the screen
// gets what moves on every frame instead: the band the bugs and the Buddy run
// in, and the Buddy's piece of sky while it is up. The other rows follow a
// slice at a time, all of them every sixth frame.
#define GAME_BAND_TOP    76
#define GAME_BAND_BOTTOM 151
#define GAME_HOP_LEFT    36
#define GAME_HOP_W       42
#define GAME_HOP_TOP     18

static void gamePushRows(int top, int bottom) {
  display->draw16bitRGBBitmap(0, top, gfx->getFramebuffer() + top * SCREEN_W, SCREEN_W, bottom - top + 1);
}

void gameFlush() {
  static const uint8_t SLICES[][2] = {{0, 15}, {16, 31}, {32, 47}, {48, 63}, {64, GAME_BAND_TOP - 1}, {GAME_BAND_BOTTOM + 1, SCREEN_H - 1}};
  static uint16_t hop[GAME_HOP_W * (GAME_BAND_TOP - GAME_HOP_TOP)];
  static uint8_t slice = 0;
  static bool wasUp = false;
  const bool up = game.height > 0;
  gamePushRows(GAME_BAND_TOP, GAME_BAND_BOTTOM);
  if (up || wasUp) {   // once more after landing, to clear where it was
    for (int row = 0; row < GAME_BAND_TOP - GAME_HOP_TOP; row++) {
      memcpy(hop + row * GAME_HOP_W, gfx->getFramebuffer() + (GAME_HOP_TOP + row) * SCREEN_W + GAME_HOP_LEFT, GAME_HOP_W * sizeof(uint16_t));
    }
    display->draw16bitRGBBitmap(GAME_HOP_LEFT, GAME_HOP_TOP, hop, GAME_HOP_W, GAME_BAND_TOP - GAME_HOP_TOP);
  }
  wasUp = up;
  gamePushRows(SLICES[slice][0], SLICES[slice][1]);
  slice = (slice + 1) % (sizeof(SLICES) / sizeof(SLICES[0]));
}

// Sends the screen only the rows that changed since the last frame, found by
// a hash of each row; all of them every few seconds and when flushForced.
void flushChanged() {
  static uint32_t rowHash[SCREEN_H], wholeAt = 0;
  uint16_t *frame = gfx->getFramebuffer();
  const bool whole = flushForced || millis() - wholeAt > 5000UL;
  if (whole) wholeAt = millis();
  flushForced = false;
  int from = -1;
  for (int y = 0; y <= SCREEN_H; y++) {
    bool changed = false;
    if (y < SCREEN_H) {
      const uint32_t *pixels = (const uint32_t *)(frame + y * SCREEN_W);
      uint32_t hash = 2166136261u;   // FNV-1a over the row, two pixels a word
      for (int i = 0; i < SCREEN_W / 2; i++) hash = (hash ^ pixels[i]) * 16777619u;
      changed = whole || hash != rowHash[y];
      rowHash[y] = hash;
    }
    if (changed && from < 0) from = y;
    if (!changed && from >= 0) {
      display->draw16bitRGBBitmap(0, from, frame + from * SCREEN_W, SCREEN_W, y - from);
      from = -1;
    }
  }
}

// ── Captive-portal screen ─────────────────────────────────
// Shown full-screen while AWM's portal is running, so the user can read
// the AP name/password off the device itself.

void drawPortalScreen() {
  gfx->fillScreen(BG);
  gfx->drawLine(0, 24, SCREEN_W, 24, rgb(0,180,255));
  centeredTextColor("WIFI SETUP", 5, 2, rgb(0,180,255));

  // Blinking "portal is live" dot, mirrors AWM's BLINK_SLOW LED state.
  if ((millis() / 500) % 2 == 0) gfx->fillCircle(SCREEN_W-16, 12, 5, rgb(0,220,120));

  // 1) Wi-Fi network name (large) -- all centred
  centeredTextColor("1  JOIN THIS WIFI", 32, 1, rgb(150,160,175));
  centeredTextColor(AP_SSID, 44, 2, FG);

  // 2) password (large)
  centeredTextColor("PASSWORD", 68, 1, rgb(150,160,175));
  centeredTextColor(AP_PASS, 80, 2, FG);

  // 3) address to open (large)
  centeredTextColor("2  OPEN IN BROWSER", 104, 1, rgb(150,160,175));
  centeredTextColor("192.168.4.1", 116, 2, rgb(0,200,255));

  // status line
  char cbuf[32];
  uint8_t clients = WiFi.softAPgetStationNum();
  snprintf(cbuf, sizeof(cbuf), "CONNECTED CLIENTS: %u", (unsigned)clients);
  centeredTextColor(cbuf, 142, 1, clients ? rgb(0,220,120) : rgb(120,120,120));
  centeredTextColor("hold screen 3s to close", 158, 1, rgb(110,110,110));
}

// While the portal is up, this owns the main loop: AWM needs update()
// called as fast as possible to serve DNS + HTTP without timeouts.
void servicePortal() {
  wifiManager.update();

  // Touch-and-hold closes the portal again (escape hatch if it was
  // opened by accident, or the router came back).
  static uint32_t holdStart = 0;
  uint16_t tx = 0, ty = 0;
  bool touching = touchReady && bsp_touch_get_coordinates(&tx, &ty);

  // Wait for the user to lift the finger that opened the portal, otherwise
  // one long press would open and immediately close it again.
  if (portalNeedsRelease) {
    if (!touching) portalNeedsRelease = false;
    touching = false;
    holdStart = 0;
  }

  if (touching) {
    if (holdStart == 0) holdStart = millis();
    else if (millis() - holdStart > TOUCH_PORTAL_HOLD_MS) {
      holdStart = 0;
      Serial.println("Closing Wi-Fi setup portal (touch hold)");
      wifiManager.closePortal();
      // Try to get back on the network straight away.
      if (wifiConfigured()) wifiManager.forzarReconexion();
      return;
    }
  } else {
    holdStart = 0;
  }

  drawPortalScreen();
  gfx->flush();
  delay(8);   // keep HTTP responsive; do not use the 24 ms UI delay here
}

// ── Wi-Fi upkeep while running ────────────────────────────
// AWM's reintentarConexionSiNecesario() is a non-blocking state machine
// with its own backoff (setReconnectBackoffMs). It only advances when
// called, so it runs on every loop pass.

void maintainWifi() {
  if (wifiManager.isPortalActive()) return;
  if (!wifiConfigured()) return;          // nothing to reconnect to
  wifiManager.reintentarConexionSiNecesario();
}

// The config.h values: the defaults for anything /config.json doesn't set,
// and everything on a board that has no /config.json (a source build).
Settings defaultsFromConfig() {
  Settings d{};
  d.mount = MOUNT_IS_RIGHT ? MOUNT_RIGHT : MOUNT_LEFT;
  d.theme      = THEME;
  strlcpy(d.githubUser, GITHUB_USER, sizeof(d.githubUser));
  strlcpy(d.name,       YOUR_NAME,  sizeof(d.name));
  // The personal touches have no config.h names: the installer sets them.
  d.greeting = GREETING_MIX;
  d.work     = {9, 17, 12};
  d.weekend  = WEEKEND_SAT_SUN;
  strlcpy(d.tzName,     TIMEZONE,    sizeof(d.tzName));
  if (strlen(TZ_RULE) > 0)
    strlcpy(d.tzRule, TZ_RULE, sizeof(d.tzRule));
  else if (!tzRuleFromOffset(TZ_OFFSET_HOURS, d.tzRule, sizeof(d.tzRule)))
    strlcpy(d.tzRule, "UTC0", sizeof(d.tzRule));
  strlcpy(d.ticker,   TICKER,   sizeof(d.ticker));
  strlcpy(d.stocks[0].symbol, TICKER, sizeof(d.stocks[0].symbol));   // config.h's ticker is the first stock
  d.stocks[0].on = true;
  d.stockCount   = 1;
  strlcpy(d.coins[0].pair, "BTC-USD", sizeof(d.coins[0].pair));
  d.coins[0].source = COIN_COINBASE;
  d.coins[0].on     = true;
  d.coinCount       = 1;
  // Before 1.2 the template's key was the placeholder "xxxxxxx": no key at all,
  // so it hides the stock page like "" does (the web flasher maps it the same).
  strlcpy(d.stockKey, strcmp(STOCKKEY, "xxxxxxx") == 0 ? "" : STOCKKEY, sizeof(d.stockKey));
  strlcpy(d.lat,      LAT,      sizeof(d.lat));
  strlcpy(d.lon,      LONG,     sizeof(d.lon));
  d.clockFormat = CLOCK_12H ? CLOCK_12H_FORMAT : CLOCK_24H;
  d.brightness = BRIGHTNESS_DEFAULT;
  d.night = {NIGHT_MODE, NIGHT_START_HOUR, NIGHT_END_HOUR, NIGHT_BRIGHTNESS};
  d.tempUnit = TEMP[0] == 'f' ? TEMP_FAHRENHEIT : TEMP_CELSIUS;
  d.windUnit = strcmp(WIND, "mph") == 0 ? WIND_MPH : WIND_KMH;
  const bool pages[SETTINGS_PAGE_COUNT] = {SHOW_FACE, SHOW_CLOCK, SHOW_DATE,
      SHOW_WEATHER, SHOW_MOON, SHOW_STOCK, SHOW_GITHUB, true};
  memcpy(d.pages, pages, sizeof(pages));
  memset(d.morePages, true, sizeof(d.morePages));   // the round as it always was: every page, the usual order
  d.pageOrderCount = 0;
  return d;
}

// settings = config.h defaults overlaid with /config.json. LittleFS must be
// mounted. Logs what it used, but never the Finnhub key itself.
void loadSettings() {
  JsonDocument stored;                   // stays null without a readable file
  bool haveFile = false;
  if (LittleFS.exists(CONFIG_PATH)) {
    File f = LittleFS.open(CONFIG_PATH, "r");
    if (f) {
      DeserializationError e = deserializeJson(stored, f);
      f.close();
      if (e) {
        Serial.printf("settings: %s unreadable (%s), using config.h\n", CONFIG_PATH, e.c_str());
        stored.clear();
      } else {
        haveFile = true;
      }
    }
  }
  settingsStored = haveFile;
  JsonDocument invalid;
  settingsLoad(defaultsFromConfig(), stored.as<JsonVariantConst>(), settings,
               settingsAdded, invalid.to<JsonObject>());
  Serial.println(haveFile ? "settings: from /config.json (config.h only fills gaps)"
                          : "settings: config.h values (no /config.json)");
  for (JsonPairConst kv : invalid.as<JsonObjectConst>())
    Serial.printf("settings: ignored invalid %s (%s)\n", kv.key().c_str(),
                  kv.value().as<const char *>());
  for (uint8_t i = 0; i < settingsAdded.count; i++)
    Serial.printf("settings: %s not in file, using default\n", settingsAdded.keys[i]);
  Serial.printf("settings: tz %s, stock_key %s\n", settings.tzRule,
                settings.stockKey[0] ? "set" : "empty");
}

// ── Serial settings protocol (the web flasher) ────────────
// "@MB <COMMAND>[ <json>]" in, "@MB <json>" out; see lib/protocol and the
// design spec. Serviced at the top of loop(), so it also answers while the
// setup hotspot is open. Nothing answers while setup() is still running.
static const char *const WIFI_PATH = "/wifi.json";
// A second saved network, same format, written by SET "wifi2". See chooseSavedWifi().
static const char *const WIFI2_PATH = "/wifi2.json";
LineReader mbReader;
uint32_t   restartAtMs = 0;    // non-zero: restart once millis() reaches it

// The request being answered. Set once per command in serviceProtocol();
// mbReply() reads it, so no handler can leave "cmd" or "id" out.
struct MbRequest {
  char    cmd[17];                // the command word as sent, up to 16 characters
  bool    hasId;
  int32_t id;
};
MbRequest mbReq = {"", false, 0};

// Sends {"ok":ok,"cmd":...,["id":...,] ...fields}, in that order. One write per
// reply, so other output can't land inside the line.
void mbReply(bool ok, JsonDocument &fields) {
  JsonDocument doc;
  doc["ok"]  = ok;
  doc["cmd"] = (const char *)mbReq.cmd;
  if (mbReq.hasId) doc["id"] = mbReq.id;
  for (JsonPair kv : fields.as<JsonObject>()) doc[kv.key()] = kv.value();
  static char out[LineReader::CAPACITY];
  memcpy(out, MB_PREFIX, 4);
  size_t n = serializeJson(doc, out + 4, sizeof(out) - 5);
  out[4 + n] = '\n';
  // HWCDC::write() takes a TX lock and, because of setTxTimeoutMs(0) in setup(),
  // returns 0 at once if another task (netTask, mid Serial.print) holds it. A
  // single write would then drop the whole reply. Retry the unwritten rest,
  // yielding 1 ms so that task can finish and release the lock. A short write
  // under host backpressure advances by what went out. Give up after about
  // 100 ms (host not reading) so the loop never stalls for long (#12).
  const uint8_t *p = (const uint8_t *)out;
  size_t left = 5 + n;
  const uint32_t start = millis();
  while (left > 0) {
    size_t w = Serial.write(p, left);
    p += w;
    left -= w;
    if (left == 0 || millis() - start >= 100) break;
    if (w == 0) delay(1);
  }
}

void mbOk() {
  JsonDocument none;
  mbReply(true, none);
}

void mbError(const char *message) {
  JsonDocument fields;
  fields["error"] = message;
  mbReply(false, fields);
}

void scheduleRestart() {
  restartAtMs = millis() + 300;           // time for the reply to leave
  if (restartAtMs == 0) restartAtMs = 1;
}

// Writes to path.tmp, then renames it over path, so a power cut leaves either
// the old file or the new one.
bool writeJsonAtomic(const char *path, JsonDocument &doc) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0 || n != measureJson(doc)) { LittleFS.remove(tmp); return false; }  // short write
  if (LittleFS.rename(tmp, path)) return true;
  // Fallback in case the filesystem won't rename over an existing file.
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}

// The saved SSID from a Wi-Fi file (AWM's plain format: {"ssid","password"}).
// Empty if there is none, or if the file can't be read as plain JSON.
// (AWM's encryption is never enabled here.)
void readSavedSsid(const char *path, char *ssid, size_t len) {
  ssid[0] = '\0';
  if (!LittleFS.exists(path)) return;   // no VFS error log on every INFO
  File f = LittleFS.open(path, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (!e && doc["ssid"].is<const char *>()) strlcpy(ssid, doc["ssid"], len);
}

// AWM knows one network, /wifi.json. With a second one saved, look at what is
// in range before AWM starts and put that one in /wifi.json. Needs LittleFS
// mounted.
// ponytail: chosen at boot only; moving between networks comes with a power
// cycle. Re-run on a failed reconnect if the board ever roams while powered.
void chooseSavedWifi() {
  static const char *const SWAP = "/wifi.swap";
  if (LittleFS.exists(SWAP)) {   // a power cut interrupted the last swap
    if (!LittleFS.exists(WIFI_PATH))       LittleFS.rename(SWAP, WIFI_PATH);
    else if (!LittleFS.exists(WIFI2_PATH)) LittleFS.rename(SWAP, WIFI2_PATH);
    else                                   LittleFS.remove(SWAP);
  }
  char first[33], second[33];
  readSavedSsid(WIFI_PATH, first, sizeof(first));
  readSavedSsid(WIFI2_PATH, second, sizeof(second));
  if (!first[0] || !second[0]) return;
  WiFi.mode(WIFI_STA);
  const int16_t found = WiFi.scanNetworks();
  bool seeFirst = false, seeSecond = false;
  for (int16_t i = 0; i < found; i++) {
    const String name = WiFi.SSID(i);
    if (name == first)  seeFirst = true;
    if (name == second) seeSecond = true;
  }
  WiFi.scanDelete();
  Serial.printf("Wi-Fi scan: %d networks, %s %s, %s %s\n", (int)found,
                first, seeFirst ? "in range" : "not seen", second, seeSecond ? "in range" : "not seen");
  if (seeFirst || !seeSecond) return;
  if (LittleFS.rename(WIFI_PATH, SWAP) && LittleFS.rename(WIFI2_PATH, WIFI_PATH)) LittleFS.rename(SWAP, WIFI2_PATH);
  Serial.printf("Wi-Fi: using %s\n", second);
}

// ── Keys ─────────────────────────────────────────────────
// The link key and the update key. "@MB KEYS" writes them, over USB only (no
// Wi-Fi message can), into KEYS_PATH: a file of their own, so DEFAULTS, a
// settings SET and the setup hotspot's erase (which removes *.json) leave
// them alone, and a firmware install over USB keeps them too. Nothing sends
// them back and nothing logs them; INFO shows a fingerprint of each, enough
// for the laptop to see whether the Buddy holds the keys it has.
static const char *const KEYS_PATH = "/keys.dat";
static char linkKeyHex[KEYS_LINK_LEN + 1]     = "";
static char updateKeyPem[KEYS_UPDATE_MAX + 1] = "";

// The stored keys over the config.h ones. LittleFS must be mounted.
void loadKeys() {
  if (keysLinkValid(LINK_KEY)) strlcpy(linkKeyHex, LINK_KEY, sizeof(linkKeyHex));
  if (keysUpdateValid(UPDATE_KEY)) strlcpy(updateKeyPem, UPDATE_KEY, sizeof(updateKeyPem));
  if (!LittleFS.exists(KEYS_PATH)) return;
  File f = LittleFS.open(KEYS_PATH, "r");
  if (!f) return;
  JsonDocument doc;
  const DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) { Serial.println("keys: the stored keys are unreadable"); return; }
  const char *link = doc["link_key"] | "";
  const char *upd  = doc["update_key"] | "";
  if (keysLinkValid(link)) strlcpy(linkKeyHex, link, sizeof(linkKeyHex));
  if (keysUpdateValid(upd)) strlcpy(updateKeyPem, upd, sizeof(updateKeyPem));
  Serial.printf("keys: link %s, update %s\n", linkKeyHex[0] ? "set" : "none", updateKeyPem[0] ? "set" : "none");
}

// 8 hex digits of SHA-256("monitor-buddy " + what + " " + key text): tells
// two keys apart without giving anything of the key away.
static void keyFingerprint(const char *what, const char *key, char out[9]) {
  uint8_t hash[32];
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  mbedtls_sha256_update(&sha, (const uint8_t *)"monitor-buddy ", 14);
  mbedtls_sha256_update(&sha, (const uint8_t *)what, strlen(what));
  mbedtls_sha256_update(&sha, (const uint8_t *)" ", 1);
  mbedtls_sha256_update(&sha, (const uint8_t *)key, strlen(key));
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);
  snprintf(out, 9, "%02x%02x%02x%02x", hash[0], hash[1], hash[2], hash[3]);
}

// {"link_key": 64 hex digits, "update_key": PEM public key}, one or both. A
// key that is left out stays as it is. Everything is checked before anything
// is written; then a restart, so the link opens with the new key.
void mbKeys(JsonDocument &req) {
  if (!req.is<JsonObject>()) { mbError("bad json"); return; }
  JsonVariantConst link = req["link_key"];
  JsonVariantConst upd  = req["update_key"];
  JsonDocument res;
  JsonObject errors = res["errors"].to<JsonObject>();
  if (link.isNull() && upd.isNull()) errors["keys"] = "send link_key, update_key or both";
  if (!link.isNull() && !(link.is<const char *>() && keysLinkValid(link.as<const char *>()))) errors["link_key"] = "must be 64 hex digits";
  if (!upd.isNull() && !(upd.is<const char *>() && keysUpdateValid(upd.as<const char *>()))) errors["update_key"] = "must be a PEM public key";
  if (!upd.isNull() && errors.size() == 0) {   // a key mbedtls cannot read would refuse every update later
    const char *pem = upd.as<const char *>();
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    if (mbedtls_pk_parse_public_key(&key, (const uint8_t *)pem, strlen(pem) + 1) != 0) errors["update_key"] = "is not a usable public key";
    mbedtls_pk_free(&key);
  }
  if (errors.size() > 0) { mbReply(false, res); return; }
  JsonDocument file;
  file["link_key"]   = link.isNull() ? (const char *)linkKeyHex : link.as<const char *>();
  file["update_key"] = upd.isNull() ? (const char *)updateKeyPem : upd.as<const char *>();
  if (!writeJsonAtomic(KEYS_PATH, file)) { mbError("could not save the keys"); return; }
  mbOk();
  Serial.printf("keys: saved%s%s, restarting\n", link.isNull() ? "" : " link", upd.isNull() ? "" : " update");
  scheduleRestart();
}

void mbInfo() {
  JsonDocument doc;
  doc["fw"]    = FW_VERSION;
  doc["cfg_v"] = CONFIG_VERSION;
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  doc["mac"] = macStr;
  JsonObject w = doc["wifi"].to<JsonObject>();
  char ssid[33];
  readSavedSsid(WIFI_PATH, ssid, sizeof(ssid));
  w["saved"] = wifiManager.tieneCredenciales();
  w["ssid"]  = (const char *)ssid;
  readSavedSsid(WIFI2_PATH, ssid, sizeof(ssid));
  if (ssid[0]) w["ssid2"] = (const char *)ssid;
  bool up = !wifiManager.isPortalActive() && wifiManager.isConnected();
  w["connected"] = up;
  if (up) w["rssi"] = wifiManager.getSignalStrength();
  // Which keys the Buddy holds: false, or a fingerprint (never the key).
  JsonObject k = doc["keys"].to<JsonObject>();
  char print[9];
  if (linkKeyHex[0]) { keyFingerprint("link", linkKeyHex, print); k["link"] = (const char *)print; } else k["link"] = false;
  if (updateKeyPem[0]) { keyFingerprint("update", updateKeyPem, print); k["update"] = (const char *)print; } else k["update"] = false;
  mbReply(true, doc);
}

void mbGet() {
  JsonDocument doc;
  doc["v"]      = CONFIG_VERSION;
  doc["stored"] = settingsStored;
  settings.brightness = brightnessLevel;   // it may have been swiped since boot
  settingsToJson(settings, doc["config"].to<JsonObject>());
  JsonArray added = doc["added"].to<JsonArray>();
  for (uint8_t i = 0; i < settingsAdded.count; i++) added.add(settingsAdded.keys[i]);
  mbReply(true, doc);
}

// {"config":{...partial...}, "wifi":{"ssid","password"}}, both optional (plus
// the request "id"). Everything is validated before anything is written; then
// a restart. `req` is the argument mbParseArg() already parsed.
void mbSet(JsonDocument &req) {
  if (!req.is<JsonObject>()) { mbError("bad json"); return; }   // SET needs a body
  JsonVariantConst cfg  = req["config"];
  JsonVariantConst wifi = req["wifi"];
  JsonVariantConst wifi2 = req["wifi2"];

  settings.brightness = brightnessLevel;   // a partial SET keeps it
  JsonDocument res;
  JsonObject errors = res["errors"].to<JsonObject>();
  Settings next = settings;
  char ssid[33], pass[64], ssid2[33], pass2[64];
  bool ok = true;
  if (!cfg.isNull() && !settingsMerge(settings, cfg, next, errors)) ok = false;
  if (!wifi.isNull() && !wifiValidate(wifi, ssid, sizeof(ssid), pass, sizeof(pass), errors)) ok = false;
  if (!wifi2.isNull() && !wifiValidate(wifi2, ssid2, sizeof(ssid2), pass2, sizeof(pass2), errors)) ok = false;
  if (!ok) { mbReply(false, res); return; }

  if (!cfg.isNull()) {
    JsonDocument file;
    settingsToJson(next, file.to<JsonObject>());
    if (!writeJsonAtomic(CONFIG_PATH, file)) { mbError("could not save settings"); return; }
    configReplaced = true;                // `next` went out with the swiped level in it
    if (next.brightness != brightnessLevel) {   // /display.json holds it, written by the restart's flush
      brightnessLevel = next.brightness;
      brightnessSavePending = true;
    }
  }
  if (!wifi.isNull()) {
    JsonDocument file;                    // AWM's plain format
    file["ssid"]     = (const char *)ssid;
    file["password"] = (const char *)pass;
    if (!writeJsonAtomic(WIFI_PATH, file)) { mbError("could not save wifi"); return; }
  }
  if (!wifi2.isNull()) {
    JsonDocument file;
    file["ssid"]     = (const char *)ssid2;
    file["password"] = (const char *)pass2;
    if (!writeJsonAtomic(WIFI2_PATH, file)) { mbError("could not save wifi2"); return; }
  }
  mbOk();
  Serial.printf("settings: saved%s%s, restarting\n",
                cfg.isNull() ? "" : " config", wifi.isNull() ? "" : " wifi");
  scheduleRestart();
}

// Deletes the saved settings, so the next boot uses the config.h values.
// Wi-Fi (/wifi.json) and brightness (/display.json) stay. With no file it is
// still ok and still restarts, which keeps the command idempotent.
void mbDefaults() {
  const String tmp = String(CONFIG_PATH) + ".tmp";
  if (LittleFS.exists(tmp)) LittleFS.remove(tmp);       // a leftover from a failed save
  if (LittleFS.exists(CONFIG_PATH) && !LittleFS.remove(CONFIG_PATH)) {
    mbError("could not reset settings");
    return;
  }
  configReplaced = true;
  mbOk();
  Serial.println("settings: reset to config.h values, restarting");
  scheduleRestart();
}

// ── Animations ───────────────────────────────────────────
// The GIFs in GIF_DIR: on the TF card when it mounts, else in the internal
// file system. One is drawn enlarged by the biggest whole factor that fits the
// screen; tools/buddy_gif.py makes them 80x43, 160x86 or 320x172. A frame may
// change only part of the picture, so nothing else draws while one plays.
#define GIF_DIR "/gifs"
static fs::FS *gifFs = &LittleFS;
static AnimatedGIF gif;
static File gifFile;
static bool gifOpen = false;
static uint32_t gifFrameAt = 0;
static int gifScale = 1, gifLeft = 0, gifTop = 0;

// Counts the animations again, on the card or else the internal file system.
void gifScan(bool card) {
  gifStop();
  gifFs = card ? (fs::FS *)&SD : (fs::FS *)&LittleFS;
  gifCount = 0;
  File dir = gifFs->open(GIF_DIR);
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (String(entry.name()).endsWith(".gif")) gifCount++;
  }
  Serial.printf("gifs: %u %s\n", (unsigned)gifCount, card ? "on the card" : "in the internal file system");
}

static void *gifOpenFile(const char *path, int32_t *size) {
  gifFile = gifFs->open(path);
  if (!gifFile) return nullptr;
  *size = gifFile.size();
  return &gifFile;
}

static void gifCloseFile(void *) { gifFile.close(); }

static char gifPlay[41] = "";   // an animation over every page until gifPlayUntil: gifShow()
static uint32_t gifPlayUntil = 0;
// The screensaver is not a file: drawBounce() draws it, with the owner's name
// from the settings. It shares gifShow()'s timing under this name. (The card's
// own bounce.gif is an ordinary animation of the GIF page.)
static const char *const SCREENSAVER = "bounce.gif";

// The owner's name (setting "name", else BUDDY) beside the Buddy's two eyes,
// in a box that bounces like the logo of an idle DVD player and changes colour
// at every wall.
void drawBounce() {
  static const uint8_t COLOURS[][3] = {{0, 200, 200}, {203, 166, 189}, {255, 196, 0}, {255, 99, 71}, {120, 220, 120}, {135, 160, 255}};
  static float x = 20, y = 12, dx = 1, dy = 1;
  static uint8_t colour = 0;
  static uint32_t last = 0;
  const char *name = settings.name[0] ? settings.name : "BUDDY";
  const int eyeW = 10, eyeH = 18, gap = 6, size = 3;
  const int width = 8 + 2 * eyeW + gap + 10 + (int)strlen(name) * 6 * size + 6, height = 8 * size + 14;
  const uint32_t now = millis();
  const float dt = (last && now - last < 200) ? (now - last) / 1000.0f : 0;
  last = now;
  x += dx * 62 * dt;
  y += dy * 41 * dt;
  if (x < 0 || x > SCREEN_W - width)  { x = constrain(x, 0.0f, (float)(SCREEN_W - width));  dx = -dx; colour++; }
  if (y < 0 || y > SCREEN_H - height) { y = constrain(y, 0.0f, (float)(SCREEN_H - height)); dy = -dy; colour++; }
  const uint8_t *c = COLOURS[colour % (sizeof(COLOURS) / sizeof(COLOURS[0]))];
  const uint16_t ink = rgb(c[0], c[1], c[2]);
  const int left = (int)x, top = (int)y;
  gfx->fillScreen(0x0000);
  gfx->drawRoundRect(left, top, width, height, 6, ink);
  gfx->drawRoundRect(left + 1, top + 1, width - 2, height - 2, 5, ink);
  for (int eye = 0; eye < 2; eye++) gfx->fillRoundRect(left + 8 + eye * (eyeW + gap), top + (height - eyeH) / 2, eyeW, eyeH, 3, ink);
  gfx->setFont();
  gfx->setTextSize(size);
  gfx->setTextColor(ink);
  gfx->setCursor(left + 8 + 2 * eyeW + gap + 10, top + 7);
  gfx->print(name);
}

// Plays GIF_DIR/name over whatever page is on, for ms.
void gifShow(const char *name, uint32_t ms) {
  const size_t len = strlen(name);
  if (len < 5 || len >= sizeof(gifPlay) || strcmp(name + len - 4, ".gif") != 0) return;
  for (size_t i = 0; i < len; i++) {
    if (!isalnum((unsigned char)name[i]) && name[i] != '.' && name[i] != '-' && name[i] != '_') return;
  }
  gifStop();
  strcpy(gifPlay, name);
  gifPlayUntil = (millis() + ms) | 1;
}

static int32_t gifReadFile(GIFFILE *file, uint8_t *buffer, int32_t length) {
  // One byte short of the end: the SD library cannot seek after reading the last one.
  if (file->iSize - file->iPos < length) length = file->iSize - file->iPos - 1;
  if (length <= 0) return 0;
  length = (int32_t)gifFile.read(buffer, length);
  file->iPos = gifFile.position();
  return length;
}

static int32_t gifSeekFile(GIFFILE *file, int32_t position) {
  gifFile.seek(position);
  file->iPos = gifFile.position();
  return file->iPos;
}

static void gifDrawLine(GIFDRAW *line) {
  uint16_t *frame = gfx->getFramebuffer();
  const int top = gifTop + (line->iY + line->y) * gifScale;
  for (int x = 0; x < line->iWidth; x++) {
    const uint8_t index = line->pPixels[x];
    if (line->ucHasTransparency && index == line->ucTransparent) continue;
    const int left = gifLeft + (line->iX + x) * gifScale;
    for (int dy = 0; dy < gifScale; dy++) {
      if (top + dy < 0 || top + dy >= SCREEN_H) continue;
      for (int dx = 0; dx < gifScale; dx++) {
        if (left + dx >= 0 && left + dx < SCREEN_W) frame[(top + dy) * SCREEN_W + left + dx] = line->pPalette[index];
      }
    }
  }
}

void gifStop() {
  if (gifOpen) gif.close();
  gifOpen = false;
}

// Opens animation number gifIndex, counted round.
static void gifStart() {
  char path[64] = "";
  uint16_t seen = 0;
  if (gifPlayUntil) snprintf(path, sizeof(path), GIF_DIR "/%s", gifPlay);
  File dir = gifFs->open(GIF_DIR);
  for (File entry = dir.openNextFile(); entry && !path[0]; entry = dir.openNextFile()) {
    if (String(entry.name()).endsWith(".gif") && seen++ == gifIndex % max<uint16_t>(gifCount, 1)) {
      snprintf(path, sizeof(path), GIF_DIR "/%s", entry.name());
    }
  }
  gfx->fillScreen(0x0000);
  gif.begin(GIF_PALETTE_RGB565_LE);
  if (!path[0] || !gif.open(path, gifOpenFile, gifCloseFile, gifReadFile, gifSeekFile, gifDrawLine)) {
    gifPlayUntil = 0;   // not there: nothing to play over the page
    return;
  }
  gifOpen = true;
  gifScale = max(1, min(SCREEN_W / gif.getCanvasWidth(), SCREEN_H / gif.getCanvasHeight()));
  gifLeft = (SCREEN_W - gif.getCanvasWidth() * gifScale) / 2;
  gifTop = (SCREEN_H - gif.getCanvasHeight() * gifScale) / 2;
  gifFrameAt = millis();
}

void drawGifs() {
  if (!gifOpen) gifStart();
  if (!gifOpen || (int32_t)(millis() - gifFrameAt) < 0) return;
  int wait = 0;
  const int more = gif.playFrame(false, &wait);
  if (more < 0) {   // a file it cannot play: the next one
    gifIndex++;
    gifPlayUntil = 0;
    gifStop();
    return;
  }
  if (more == 0) gif.reset();
  gifFrameAt = millis() + max(wait, 20);
}

// Shows page `page` (a PAGE_* id) as a swipe would, for the PC.
void showPage(int page) {
  if (page < 0 || page > PAGE_YESTERDAY) return;
  currentApp = (uint8_t)page;
  todayUntil = yesterdayUntil = 0;
  todayTitle = "TODAY";
  for (uint8_t i = 0; i < pageCount; i++) if (pageOrder[i] == currentApp) currentPageIdx = i;
  nextAutoPage = millis() + PAGE_AUTO_INTERVAL_MS;
}

// The PC's STATE message, from USB or the Wi-Fi link.
void mbState(JsonDocument &body) {
  const bool wasUnlocked = pc.known && !pc.locked;
  pc.known      = true;
  pc.locked     = body["locked"] | false;
  privacyLock   = pc.locked;
  pc.mic        = body["mic"] | false;
  pc.reviews    = body["reviews"] | 0;
  pc.changes    = body["changes"] | 0;
  pc.useKnown   = body["use"].is<JsonArrayConst>();
  pc.use5h      = body["use"][0] | 0;
  pc.use7d      = body["use"][1] | 0;
  for (int i = 0; i < 2; i++) odooAscii(body["use_reset"][i] | "", pc.useReset[i], sizeof(pc.useReset[i]), sizeof(pc.useReset[i]) - 1);
  pc.sessionCount = 0;
  for (JsonVariantConst session : body["sessions"].as<JsonArrayConst>()) {
    if (pc.sessionCount >= PC_SESSION_MAX) break;
    PcSession &slot = pc.sessions[pc.sessionCount++];
    odooAscii(session[0] | "", slot.project, sizeof(slot.project), sizeof(slot.project) - 1);
    slot.state = session[1] | 0;
    slot.since = session[2] | 0UL;
  }
  odooAscii(body["at"] | "", pc.project, sizeof(pc.project), sizeof(pc.project) - 1);
  odooAscii(body["in"] | "", pc.in, sizeof(pc.in), sizeof(pc.in) - 1);
  odooAscii(body["out"] | "", pc.out, sizeof(pc.out), sizeof(pc.out) - 1);
  odooAscii(body["yin"] | "", pc.yin, sizeof(pc.yin), sizeof(pc.yin) - 1);
  odooAscii(body["yout"] | "", pc.yout, sizeof(pc.yout), sizeof(pc.yout) - 1);
  pc.prs        = body["prs"] | 0;
  pc.merged     = body["prs_merged"] | 0;
  pc.reviewed   = body["prs_reviewed"] | 0;
  const int failingBefore = pc.prsFailing;
  pc.prsFailing = body["prs_failing"] | 0;
  if (wasUnlocked && failingBefore > 0 && pc.prsFailing == 0) gifShow(GIF_CI_GREEN, 7000);
  pc.claude     = body["claude"] | 0;
  pc.prCount    = 0;
  for (JsonVariantConst pr : body["pr_list"].as<JsonArrayConst>()) {
    if (pc.prCount >= PR_LIST_MAX) break;
    PcPr &slot = pc.prList[pc.prCount++];
    odooAscii(pr["t"] | "", slot.title, sizeof(slot.title), sizeof(slot.title) - 1);
    slot.check = pr["s"] | 0;
    slot.review = pr["r"] | 0;
  }
  pc.at         = millis();
  // The Today page by itself: at the day's first sign of an unlocked PC from
  // 05:00, after the Yesterday page, and whenever the PC is locked from 17:00.
  dayRoll();
  if (clockTrusted && !pc.locked && !day.briefed && localNow().hour >= 5) {
    day.briefed = true;
    daySave();
    todayShow(localNow().hour < 12 ? "GOOD MORNING" : "HELLO", YESTERDAY_MS + 20000);
    yesterdayUntil = (millis() + YESTERDAY_MS) | 1;
  } else if (clockTrusted && pc.locked && wasUnlocked && localNow().hour >= settings.work.end) {
    todayShow("DAY DONE", 30000);
  }
  if (body["timer"].is<bool>()) timerSet(body["timer"].as<bool>(), true);   // the PC starts or stops the timer
  // "mood": show that face now, to try one out; the next status change replaces it.
  if (body["mood"].is<int>() && body["mood"].as<int>() >= 0 && body["mood"].as<int>() < MOOD_TOTAL) {
    faceMood = (uint8_t)body["mood"].as<int>();
    currentPageIdx = 0;
    currentApp = pageOrder[0];
  }
  // For screenshots: "demo" (see PcState), "page" shows that PAGE_* id, "tap" taps it.
  pc.demo = body["demo"] | 0;
  if (body["wave"] | false) waveUntil = (millis() + 8000) | 1;   // "meet your Buddy": the installer's finale
  if (body["page"].is<int>()) showPage(body["page"].as<int>());
  if (body["play"].is<const char *>()) gifShow(body["play"], 7000);   // to try one out
  if (body["tap"] | false) {
    lastTapMs = 0;
    if (currentApp == PAGE_GAME) gameTap(); else triggerFaceTap();
  }
}

// The Wi-Fi link: tools/buddy_link.py and tools/buddy_flash.py reach the Buddy
// with UDP packets of
//   nonce (12) | AES-256-GCM of the message | tag (16),
// keyed with LINK_KEY. The nonce starts with the PC's clock in milliseconds,
// big-endian. A packet has to be newer than the last one taken and within
// LINK_SKEW_S of this clock, so a recorded packet cannot be played back. The
// answer is "ok", the first 8 bytes of this firmware's ELF SHA-256, a status
// byte (bit 0: the timer runs, bit 1: the animations come from the card; in the
// answer to P or STATE, once each: bit 2 join the meeting on the face, bit 3
// lock the PC) and for H and C a JSON text, sealed under the same nonce with
// its top bit set.
//   {...}              the PC's STATE
//   P                  ping
//   S text             show that line for a while (buddy-say)
//   O {...}            Odoo data: odooFromPc()
//   H                  how the Buddy is: healthJson()
//   C                  test the card: sdTest()
//   G page             show that PAGE_* page
//   R row              rows `row` and `row` + 1 of the screen, RGB565 (tools/buddy_shot.py)
//   B size(4) sig      begin a firmware update of `size` bytes; sig: its ECDSA P-256
//                      signature (DER) by the key UPDATE_KEY is the public half of
//   F size(4) name     begin an animation for GIF_DIR, `name` like starfield.gif
//   D offset(4) bytes  the next piece of the update or animation
//   X name             remove that animation
// Every other command stays on USB.
#define LINK_PORT           4747
#define LINK_PACKET_MAX     1400
#define LINK_SKEW_S         60
#define LINK_UPDATE_IDLE_MS 10000UL   // an update that goes silent is dropped

static WiFiUDP linkUdp;
static mbedtls_gcm_context linkGcm;
static uint8_t linkPacket[LINK_PACKET_MAX];   // keeps the nonce for linkAnswer()
static uint8_t linkText[LINK_PACKET_MAX];

// The core would keep a firmware that came by update as soon as it starts.
// serviceLink() keeps it once the link works again, so the bootloader goes
// back to the firmware before one that cannot be reached.
extern "C" bool verifyRollbackLater() { return true; }

static bool linkKey(uint8_t key[32]) {
  if (!keysLinkValid(linkKeyHex)) return false;
  for (int i = 0; i < 32; i++) {
    unsigned value;
    sscanf(linkKeyHex + 2 * i, "%2x", &value);
    key[i] = (uint8_t)value;
  }
  return true;
}

static uint32_t linkNumber(const uint8_t *bytes) {
  return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

// Opens one waiting packet into linkText: its length, or -1 for none or one
// that is refused.
static int linkRead() {
  static uint64_t lastMs = 0;
  const int size = linkUdp.parsePacket();
  if (size <= 0) return -1;
  const int n = linkUdp.read(linkPacket, sizeof(linkPacket));
  linkUdp.clear();
  if (size > (int)sizeof(linkPacket) || n < 12 + 1 + 16) return -1;
  const int len = n - 12 - 16;
  if (mbedtls_gcm_auth_decrypt(&linkGcm, len, linkPacket, 12, nullptr, 0, linkPacket + n - 16, 16,
                               linkPacket + 12, linkText) != 0) return -1;
  uint64_t ms = 0;
  for (int i = 0; i < 8; i++) ms = ms << 8 | linkPacket[i];
  if (ms <= lastMs || llabs((long long)(ms / 1000) - (long long)time(nullptr)) > LINK_SKEW_S) {
    Serial.println("link: old packet dropped (is the clock right on both sides?)");
    return -1;
  }
  lastMs = ms;
  return len;
}

// Answers the packet linkRead() opened last.
static void linkAnswer(const char *extra = nullptr, size_t extraLen = 0) {
  static uint8_t text[LINK_PACKET_MAX], sealed[LINK_PACKET_MAX + 16];
  const size_t more = extra ? min(extraLen ? extraLen : strlen(extra), (size_t)LINK_PACKET_MAX - 11) : 0;
  text[0] = 'o';
  text[1] = 'k';
  memcpy(text + 2, esp_app_get_description()->app_elf_sha256, 8);
  text[10] = (timerStartMs != 0) | (gifFs != &LittleFS) << 1;
  if (linkText[0] == 'P' || linkText[0] == '{') {   // buddy_link.py's rounds act on the requests
    text[10] |= (joinAskedAt != 0) << 2 | (lockAskedAt != 0) << 3;
    joinAskedAt = lockAskedAt = 0;
  }
  memcpy(text + 11, extra, more);
  linkPacket[0] |= 0x80;
  if (mbedtls_gcm_crypt_and_tag(&linkGcm, MBEDTLS_GCM_ENCRYPT, 11 + more, linkPacket, 12, nullptr, 0,
                                text, sealed, 16, sealed + 11 + more) == 0
      && linkUdp.beginPacket(linkUdp.remoteIP(), linkUdp.remotePort())) {
    linkUdp.write(sealed, 11 + more + 16);
    linkUdp.endPacket();
  }
}

// Takes D pieces from `written` on, hands them to `sink` and counts them in
// `written`. True once `size` bytes are in: the caller answers the last piece
// when it has checked what it got.
static bool linkTake(uint32_t size, uint32_t &written, const char *title, size_t (*sink)(const uint8_t *, size_t)) {
  uint32_t heardAt = millis();
  int shown = -1;
  while (written < size && millis() - heardAt < LINK_UPDATE_IDLE_MS) {
    const int percent = (int)(written * 100ULL / size);
    if (percent / 5 != shown) {
      shown = percent / 5;
      char line[8];
      snprintf(line, sizeof(line), "%d%%", shown * 5);
      drawBootMessage(title, line, nullptr);
    }
    const int len = linkRead();
    if (len < 5 || linkText[0] != 'D') { delay(1); continue; }
    const uint32_t offset = linkNumber(linkText + 1), count = len - 5;
    if (offset == written && count > 0 && count <= size - written) {
      if (sink(linkText + 5, count) != count) break;
      written += count;
      heardAt = millis();
      if (written == size) break;
    } else if (offset + count != written) {
      continue;   // neither the next piece nor the one before it, sent again
    }
    linkAnswer();
  }
  return written == size;
}

static mbedtls_sha256_context updateSha;
// The update being taken. One the link lost stays open for UPDATE_RESUME_MS:
// the same B again goes on where it stopped.
#define UPDATE_RESUME_MS (10UL * 60UL * 1000UL)
static struct { bool open; uint32_t size, written, pausedAt; uint8_t sig[80]; size_t sigLen; } update = {};

static size_t linkToUpdate(const uint8_t *bytes, size_t count) {
  mbedtls_sha256_update(&updateSha, bytes, count);
  return Update.write((uint8_t *)bytes, count);
}

static void updateClose() {
  Update.abort();
  mbedtls_sha256_free(&updateSha);
  update.open = false;
}

// A firmware image goes into the other app slot. It is kept only when its
// signature checks out against UPDATE_KEY; then a restart into it. The answer
// to B says from which byte on the pieces go.
static void linkUpdate(uint32_t size, const uint8_t *signature, size_t signatureLen) {
  const bool same = update.open && update.size == size && update.sigLen == signatureLen
                    && memcmp(update.sig, signature, signatureLen) == 0;
  if (!same) {
    if (update.open) updateClose();
    if (!updateKeyPem[0] || signatureLen > sizeof(update.sig) || !Update.begin(size)) {
      Serial.printf("link: update refused: %s\n", updateKeyPem[0] ? Update.errorString() : "no update key");
      return;
    }
    update = {true, size, 0, 0, {}, signatureLen};
    memcpy(update.sig, signature, signatureLen);
    mbedtls_sha256_init(&updateSha);
    mbedtls_sha256_starts(&updateSha, 0);
    Serial.printf("link: update of %u bytes\n", (unsigned)size);
  } else {
    Serial.printf("link: update goes on at %u of %u\n", (unsigned)update.written, (unsigned)size);
  }
  char from[12];
  snprintf(from, sizeof(from), "%lu", (unsigned long)update.written);
  linkAnswer(from);
  if (!linkTake(size, update.written, "UPDATING", linkToUpdate)) {
    if (Update.hasError()) {
      Serial.printf("link: update dropped: %s\n", Update.errorString());
      updateClose();
      return;
    }
    update.pausedAt = millis() | 1;
    Serial.printf("link: update paused at %u of %u\n", (unsigned)update.written, (unsigned)size);
    return;
  }
  uint8_t hash[32];
  mbedtls_sha256_finish(&updateSha, hash);
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const bool genuine = mbedtls_pk_parse_public_key(&key, (const uint8_t *)updateKeyPem, strlen(updateKeyPem) + 1) == 0
                       && mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, hash, sizeof(hash), update.sig, update.sigLen) == 0;
  mbedtls_pk_free(&key);
  if (genuine && Update.end()) {
    linkAnswer();
    Serial.println("link: update stored, restarting");
    drawBootMessage("UPDATED", "restarting...", nullptr);
    delay(300);
    ESP.restart();
  }
  linkAnswer("refused");
  Serial.printf("link: update refused: %s\n", genuine ? Update.errorString() : "the signature does not match");
  updateClose();
}

// GIF_DIR/name for an animation's file name of letters, digits, dot, dash and
// underscore that ends in .gif; false for anything else.
static bool linkGifPath(const uint8_t *name, int len, char *path, size_t size) {
  if (len < 5 || len > 40 || name[0] == '.' || memcmp(name + len - 4, ".gif", 4) != 0) return false;
  for (int i = 0; i < len; i++) {
    if (!isalnum(name[i]) && name[i] != '.' && name[i] != '-' && name[i] != '_') return false;
  }
  snprintf(path, size, GIF_DIR "/%.*s", len, (const char *)name);
  return true;
}

static File linkFile;
static size_t linkToFile(const uint8_t *bytes, size_t count) { return linkFile.write(bytes, count); }

// An animation arrives under another name and takes its own once complete.
static void linkGif(uint32_t size, const uint8_t *name, int len) {
  char path[64], part[70];
  const bool card = gifFs != &LittleFS;
  if (!linkGifPath(name, len, path, sizeof(path)) || size == 0
      || (!card && size + 16384 > LittleFS.totalBytes() - LittleFS.usedBytes())) {
    Serial.println("link: animation refused (name or room)");
    return;
  }
  gifStop();
  snprintf(part, sizeof(part), "%s.part", path);
  gifFs->mkdir(GIF_DIR);
  linkFile = gifFs->open(part, FILE_WRITE);
  uint32_t written = 0;
  if (linkFile) linkAnswer();
  const bool whole = linkFile && linkTake(size, written, "RECEIVING", linkToFile);
  linkFile.close();
  if (whole) {
    gifFs->remove(path);
    if (gifFs->rename(part, path)) linkAnswer();
  } else {
    gifFs->remove(part);
  }
  gifScan(card);
}

// How the Buddy is doing, for tools/buddy_check.py.
static String healthJson() {
  JsonDocument doc;
  doc["up_s"]        = millis() / 1000;
  doc["reset"]       = (int)esp_reset_reason();
  doc["heap"]        = ESP.getFreeHeap();
  doc["heap_min"]    = ESP.getMinFreeHeap();
  doc["rssi"]        = WiFi.RSSI();
  doc["clock_set"]   = clockTrusted;
  doc["slot"]        = esp_ota_get_running_partition()->label;
  doc["fs_kb"]       = (uint32_t)(LittleFS.usedBytes() / 1024);
  doc["fs_total_kb"] = (uint32_t)(LittleFS.totalBytes() / 1024);
  doc["gifs"]        = gifCount;
  doc["gifs_from"]   = gifFs == &LittleFS ? "internal" : "card";
  doc["odoo_age_s"]  = netView.odoo.valid ? (long)(millisSince(netView.odoo.updatedAt) / 1000) : -1L;
  doc["weather"]     = netView.weather.valid;
  doc["github"]      = netView.github.valid;
  doc["pc_locked"]   = privacyLock;
  doc["crash_dump"]  = esp_core_dump_image_check() == ESP_OK;
  doc["double_knocks"] = knockCount;
  JsonArray knocks = doc["last_knocks_g"].to<JsonArray>();   // newest first
  const uint32_t starts = knockStarts;
  for (uint32_t i = 1; i <= starts && i <= 8; i++) knocks.add(roundf(knockRing[(starts - i) % 8] * 100) / 100);
  String text;
  serializeJson(doc, text);
  return text;
}

void serviceLink() {
  static bool listening = false, onTrial = false;
  if (!listening) {
    uint8_t key[32];
    if (!wifiManager.isConnected() || !linkKey(key) || !linkUdp.begin(LINK_PORT)) return;
    mbedtls_gcm_init(&linkGcm);
    if (mbedtls_gcm_setkey(&linkGcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0) {
      mbedtls_gcm_free(&linkGcm);
      return;
    }
    esp_ota_img_states_t slot;
    onTrial = esp_ota_get_state_partition(esp_ota_get_running_partition(), &slot) == ESP_OK
              && slot == ESP_OTA_IMG_PENDING_VERIFY;
    listening = true;
    Serial.printf("link: listening on UDP %d%s\n", LINK_PORT, onTrial ? ", new firmware on trial" : "");
  }
  if (update.open && update.pausedAt && millisSince(update.pausedAt) > UPDATE_RESUME_MS) {
    Serial.println("link: paused update given up");
    updateClose();
  }
  const int len = linkRead();
  if (len < 0) return;
  if (onTrial) {
    onTrial = false;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("link: new firmware kept");
  }
  if (len == 1 && linkText[0] == 'P') {
    linkAnswer();
  } else if (len == 2 && linkText[0] == 'R') {
    const uint8_t row = linkText[1];
    if (row >= SCREEN_H) return;
    const int rows = min(2, SCREEN_H - (int)row);
    linkAnswer((const char *)(gfx->getFramebuffer() + row * SCREEN_W), rows * SCREEN_W * 2);
  } else if (len == 2 && linkText[0] == 'G') {
    showPage(linkText[1]);
    nextAutoPage = millis() + 30000UL;   // long enough for a screenshot
    linkAnswer();
  } else if (len == 1 && linkText[0] == 'H') {
    linkAnswer(healthJson().c_str());
  } else if (len == 1 && linkText[0] == 'C') {
    JsonDocument req, doc;
    sdTest(req, doc);
    String text;
    serializeJson(doc, text);
    linkAnswer(text.c_str());
  } else if (len > 1 && linkText[0] == 'O') {
    JsonDocument body;
    if (deserializeJson(body, (const char *)linkText + 1, len - 1) != DeserializationError::Ok) return;
    odooFromPc(body);
    linkAnswer();
  } else if (len > 1 && linkText[0] == 'S') {
    linkText[len] = '\0';
    noticeShow((const char *)linkText + 1);
    linkAnswer();
  } else if (len > 5 && len <= 5 + 80 && linkText[0] == 'B') {
    linkUpdate(linkNumber(linkText + 1), linkText + 5, len - 5);
  } else if (len > 5 && linkText[0] == 'F') {
    linkGif(linkNumber(linkText + 1), linkText + 5, len - 5);
  } else if (linkText[0] == 'X') {
    char path[64];
    if (!linkGifPath(linkText + 1, len - 1, path, sizeof(path))) return;
    gifStop();
    gifFs->remove(path);
    gifScan(gifFs != &LittleFS);
    linkAnswer();
  } else if (linkText[0] == '{') {
    JsonDocument body;
    if (deserializeJson(body, (const char *)linkText, len) != DeserializationError::Ok) return;
    mbState(body);
    linkAnswer();
  }
}

// Tests the card in the TF slot: does it answer, does it hold a FAT file
// system, and do SD_TEST_BYTES written to a file read back the same. It mounts
// the card again each time, so one put in after the start is found.
#define SD_HZ         20000000
#define SD_TEST_BYTES (256UL * 1024)
static uint8_t sdTestByte(uint32_t at) { return (uint8_t)(at * 31 >> 3); }

bool sdTest(JsonDocument &req, JsonDocument &doc) {
  const uint32_t hz = req["hz"] | SD_HZ;   // a slower bus, to tell a bad card from bad wiring
  static const char *const TYPES[] = {"none", "MMC", "SD", "SDHC", "unknown"};
  static const char *const TEST_PATH = "/buddy-test.bin";
  static uint8_t block[4096];
  gifScan(false);   // off the card while it is tested
  SD.end();
  const uint8_t drive = sdcard_init(SD_CS, &SPI, hz);
  const bool answers = drive != 0xFF && !(disk_initialize(drive) & STA_NOINIT);
  doc["card"] = TYPES[answers ? min((int)sdcard_type(drive), 4) : 0];
  if (answers) {   // what the first sector and the first partition's boot sector say
    doc["size_mb"] = (uint32_t)((uint64_t)sdcard_num_sectors(drive) * sdcard_sector_size(drive) >> 20);
    char name[9] = "";
    if (disk_read(drive, block, 0, 1) != RES_OK) {
      doc["sector0"] = "read error";
    } else {
      const uint32_t start = block[0x1C6] | block[0x1C7] << 8 | block[0x1C8] << 16 | (uint32_t)block[0x1C9] << 24;
      doc["signature"] = block[0x1FE] == 0x55 && block[0x1FF] == 0xAA;
      doc["partition_type"] = block[0x1C2];
      doc["partition_start"] = start;
      odooAscii((const char *)block + 3, name, sizeof(name), 8);
      doc["sector0"] = (const char *)name;
      if (start > 0 && start < sdcard_num_sectors(drive) && disk_read(drive, block, start, 1) == RES_OK) {
        odooAscii((const char *)block + 3, name, sizeof(name), 8);
        doc["partition"] = (const char *)name;
      }
    }
  }
  if (drive != 0xFF) sdcard_uninit(drive);
  if (!answers) { doc["error"] = "no card answers"; return false; }
  if (!SD.begin(SD_CS, SPI, hz)) { doc["error"] = "the card answers but holds no FAT file system"; return false; }
  doc["used_mb"] = (uint32_t)(SD.usedBytes() >> 20);

  uint32_t started = millis();
  File file = SD.open(TEST_PATH, FILE_WRITE);
  bool ok = (bool)file;
  for (uint32_t at = 0; ok && at < SD_TEST_BYTES; at += sizeof(block)) {
    for (size_t i = 0; i < sizeof(block); i++) block[i] = sdTestByte(at + i);
    ok = file.write(block, sizeof(block)) == sizeof(block);
  }
  file.close();
  doc["write_kb_s"] = SD_TEST_BYTES / 1024 * 1000 / max<uint32_t>(millis() - started, 1);
  started = millis();
  file = SD.open(TEST_PATH, FILE_READ);
  ok = ok && file;
  for (uint32_t at = 0; ok && at < SD_TEST_BYTES; at += sizeof(block)) {
    ok = file.read(block, sizeof(block)) == sizeof(block);
    for (size_t i = 0; ok && i < sizeof(block); i++) ok = block[i] == sdTestByte(at + i);
  }
  file.close();
  doc["read_kb_s"] = SD_TEST_BYTES / 1024 * 1000 / max<uint32_t>(millis() - started, 1);
  SD.remove(TEST_PATH);
  doc["test"] = ok ? "passed" : "failed";
  gifScan(true);
  return ok;
}

void mbSd(JsonDocument &req) {
  JsonDocument doc;
  const bool ok = sdTest(req, doc);
  mbReply(ok, doc);
}

void serviceProtocol() {
  while (Serial.available() > 0) {
    LineReader::Result r = mbReader.feed((char)Serial.read());
    if (r == LineReader::TOO_LONG) {
      // Too long to parse, so no id; "cmd" is the first word of the line.
      mbReq.hasId = false;
      mbCommandWord(mbReader.line(), mbReq.cmd, sizeof(mbReq.cmd));
      mbError(restartAtMs != 0 ? "restarting" : "line too long");
      continue;
    }
    if (r != LineReader::LINE) continue;
    const char *arg = "";
    MbCommand cmd = mbParse(mbReader.line(), &arg);
    if (cmd == MB_NONE) continue;

    mbCommandWord(mbReader.line(), mbReq.cmd, sizeof(mbReq.cmd));
    JsonDocument body;                    // the parsed argument, null when empty
    MbArgResult parsed = mbParseArg(arg, body, mbReq.hasId, mbReq.id);
    // A restart is pending: answer "restarting" instead of acting. A second
    // SET would merge into the stale boot-time settings and lose the first
    // one's changes; a GET would report values that are about to change.
    // The id is echoed only if the argument parsed and held a valid one.
    if (restartAtMs != 0) { mbError("restarting"); continue; }
    if (parsed == MB_ARG_BAD_JSON) { mbError("bad json"); continue; }
    if (parsed == MB_ARG_BAD_ID)   { mbError("bad id"); continue; }
    switch (cmd) {
      case MB_INFO:     mbInfo(); break;
      case MB_GET:      mbGet(); break;
      case MB_SET:      mbSet(body); break;
      case MB_REBOOT:   mbOk(); scheduleRestart(); break;
      case MB_DEFAULTS: mbDefaults(); break;
      case MB_KEYS:     mbKeys(body); break;
      case MB_STATE:    mbState(body); mbOk(); break;
      case MB_SD:       mbSd(body); break;
      case MB_ODOO:     odooFromPc(body); mbOk(); break;
      case MB_SHOT: {
        // The frame on screen, a line of base64 per row of little-endian RGB565.
        const uint8_t *frame = (const uint8_t *)gfx->getFramebuffer();
        static unsigned char row[SCREEN_W * 2 * 4 / 3 + 24];
        for (int y = 0; y < SCREEN_H; y++) {
          size_t n = 0;
          const int head = snprintf((char *)row, 12, "@SHOT %d ", y);
          mbedtls_base64_encode(row + head, sizeof(row) - head - 2, &n, frame + (size_t)y * SCREEN_W * 2, SCREEN_W * 2);
          n += head;
          row[n++] = '\n';
          // The USB serial buffer is small and drops what does not fit: send
          // 64 bytes at a time and wait for whatever was not taken.
          for (size_t sent = 0; sent < n;) {
            const size_t took = Serial.write(row + sent, n - sent < 64 ? n - sent : 64);
            sent += took;
            if (took == 0) delay(2);
          }
        }
        mbOk();
        break;
      }
      default:          mbError("unknown command"); break;
    }
  }
  if (restartAtMs != 0 && (int32_t)(millis() - restartAtMs) >= 0) {
    flushBrightnessSave(true);            // don't lose a just-swiped brightness
    flushNightSave(true);
    ESP.restart();
  }
}

void saveDisplaySettings() {
  JsonDocument doc;
  doc["brightness"] = brightnessLevel;
  File file = LittleFS.open("/display.json", "w");
  if (!file) return;
  serializeJson(doc, file);
  file.close();
}

void loadDisplaySettings() {
  if (!LittleFS.exists("/display.json")) return;
  File file = LittleFS.open("/display.json", "r");
  if (!file) return;
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (error) return;
  int level = doc["brightness"] | BRIGHTNESS_DEFAULT;
  brightnessLevel = (uint8_t)constrain(level, BRIGHTNESS_MINIMUM, 255);
}

void setBrightness(int level) {
  brightnessLevel = (uint8_t)constrain(level, BRIGHTNESS_MINIMUM, 255);
  nightBanner = false;
  brightnessSavePending = true;           // written by flushBrightnessSave()
  brightnessChangedMs   = millis();
  brightnessBannerUntil = (millis() + 1200) | 1;   // 0 means no banner
  Serial.printf("Brightness: %u/255\n", brightnessLevel);
}

// Writes a pending brightness change once the drag has been still for
// BRIGHTNESS_SAVE_DELAY_MS, or at once when `now` is set. Skips the write
// when the level ended where it started.
void flushBrightnessSave(bool now) {
  if (!brightnessSavePending) return;
  if (!now && millis() - brightnessChangedMs < BRIGHTNESS_SAVE_DELAY_MS) return;
  brightnessSavePending = false;
  if (brightnessLevel == savedBrightness) return;
  saveDisplaySettings();
  savedBrightness = brightnessLevel;
  Serial.printf("Brightness saved: %u/255\n", brightnessLevel);
}

// ── Interaction handlers ──────────────────────────────────

void triggerDoubleTap() {
  autoPageEnabled = !autoPageEnabled;
  autoPageBannerUntil = (millis() + 1400) | 1;     // 0 means no banner
  if (autoPageEnabled) nextAutoPage = millis() + PAGE_AUTO_INTERVAL_MS;
  Serial.printf("Auto-advance: %s\n", autoPageEnabled ? "ON" : "OFF");
}

void triggerFaceTap() {
  uint32_t now = millis();
  if (noticeUntil) { noticeUntil = 0; return; }   // a tap puts the notice away
  if (gifPlayUntil) { gifPlayUntil = 1; return; }  // and an animation that plays over the page
  if (privacyLock) return;
  if (currentApp == PAGE_GAME) return;            // readTouch() gave the press to the game
  // Double-tap detection: two taps within 400 ms
  if (now - lastTapMs < 400UL) {
    tapCount++;
    if (tapCount >= 2) {
      tapCount = 0;
      lastTapMs = 0;
      triggerDoubleTap();
      return;
    }
  } else {
    tapCount = 1;
  }
  lastTapMs = now;
  // Single tap: the next entry on a page that has several (face moods, the
  // stocks that are on), else the next page (spec stocks-and-coins §5.1).
  if (currentApp == PAGE_FACE && faceStatus == 12) {   // MEETING IN n MIN: buddy_link.py opens its link
    joinAskedAt = millis() | 1;
    joinShownUntil = (millis() + JOIN_SHOW_MS) | 1;
    pressPulse = 1.0f;
    Serial.println("tap: join the meeting");
  } else if (currentApp == PAGE_FACE) {
    faceMood = (faceMood + 1) % FACE_MOOD_COUNT;
    pressPulse = 1.0f;
  } else if (currentApp == PAGE_STOCK && stockActiveCount >= 2) {
    stockSub = (stockSub + 1) % stockActiveCount;
    pressPulse = 1.0f;
  } else if (currentApp == PAGE_COIN && coinActiveCount >= 2) {
    coinSub = (coinSub + 1) % coinActiveCount;
    pressPulse = 1.0f;
  } else if (currentApp == PAGE_ODOO && netView.odoo.taskCount >= 2) {
    odooSub = (odooSub + 1) % netView.odoo.taskCount;
    pressPulse = 1.0f;
  } else if (currentApp == PAGE_GIFS) {
    gifIndex++;
    gifStop();
  } else if (currentApp == PAGE_GITHUB && netView.github.haveActivity) {
    githubSub ^= 1;
    pressPulse = 1.0f;
  } else {
    switchApp(1);
  }
  nextAutoPage = millis() + PAGE_AUTO_INTERVAL_MS;
}

void readSensors() {
  if (!imuReady) {
    // Animate eyes sinusoidally when IMU is absent
    filteredAx = sin(millis()*0.0012f)*0.12f;
    filteredAy = cos(millis()*0.0010f)*0.12f;
    return;
  }
  if (!knockWatching) imu.update();   // knockTask() keeps them fresh
  imu.getAccel(&accel);
  imu.getGyro(&gyro);
  filteredAx = filteredAx*0.88f + accel.accelX*0.12f;
  filteredAy = filteredAy*0.88f + accel.accelY*0.12f;
  filteredAz = filteredAz*0.88f + accel.accelZ*0.12f;
  filteredGz = filteredGz*0.82f + gyro.gyroZ*0.18f;
  // A shake: the pull on the Buddy jumps by more than 0.9 g between two
  // readings, four times within 0.8 s. Every further jolt keeps it going.
  // updateBuddyMood() answers it.
  static float lastPull = 1.0f;
  static uint32_t jolts[4] = {0, 0, 0, 0};
  static uint8_t joltAt = 0;
  const float pull = sqrtf(accel.accelX * accel.accelX + accel.accelY * accel.accelY + accel.accelZ * accel.accelZ);
  if (fabsf(pull - lastPull) > 0.9f) {
    const uint32_t oldest = jolts[joltAt];
    jolts[joltAt] = millis() | 1;
    joltAt = (joltAt + 1) % 4;
    if (shakeAtMs || (oldest && millis() - oldest < 800UL)) shakeAtMs = millis() | 1;
  }
  lastPull = pull;
  // Fast spin → surprised face
  if (fabs(filteredGz) > 130.0f) { faceMood = 2; pressPulse = 1.0f; }
}

static uint32_t touchSeenAt = 0;   // the last frame the panel reported a finger

// A BOOT press asks buddy_link.py to lock the PC.
static void readBootButton() {
  static ButtonEdge boot{300};
  if (!boot.press(digitalRead(BOOT_PIN) == LOW, millis())) return;
  lockAskedAt = millis() | 1;
  Serial.println("boot: lock the PC");
}

void readTouch() {
  if (!touchReady) return;
  //if (!touchWasDown && TOUCH_INT != 255 && digitalRead(TOUCH_INT) != LOW) return;
  uint16_t x = 0, y = 0;
  bsp_touch_read();
  if (bsp_touch_get_coordinates(&x, &y)) {
    uint32_t now = millis();
    const bool dimming = nightDimming();  // first: this retires a wake that has run out
    if (nightWoken) nightWokenAt = now;   // still in use: keep the night wake going
    touchLastX = x; touchLastY = y;
    touchMissFrames = 0;
    touchSeenAt = now;
    if (!touchWasDown) {
      touchStartX = x; touchStartY = y; touchStartMs = now;
      touchMoved = false;
      touchSwipeDone = false;
      touchWasDown = true;
      touchDark = dimming;
      touchNightStepped = false;
      touchPortalArmed = !dimming;   // this press is eligible to open the portal
      if (currentApp == PAGE_GAME && !dimming && !noticeUntil && !privacyLock) gameTap();   // at once, not on release
      return;
    }
    // A page swipe already fired for this press: the rest of the contact is
    // part of that swipe, not a new press, until the finger lifts.
    if (touchSwipeDone) return;
    int16_t dx = (int16_t)x-(int16_t)touchStartX;
    int16_t dy = (int16_t)y-(int16_t)touchStartY;
    if (abs(dx) > 12 || abs(dy) > 12) touchMoved = true;

    // Touch-and-hold (still, ~2.5 s) → open the Wi-Fi setup portal.
    // Fires once per press; a swipe cancels it.
    if (touchPortalArmed && !dimming && !privacyLock && !touchMoved && (now - touchStartMs) > TOUCH_PORTAL_HOLD_MS) {
      touchPortalArmed = false;
      openSetupPortal("touch hold");
      return;
    }

    // A swipe keeps the press alive (touchWasDown stays true) and marks it as
    // moved, so holding still afterwards can't open the portal and lifting
    // can't count as a tap. The press ends only when the finger lifts.
    if (abs(dx) > 55 && abs(dx) > abs(dy)+18) {
      if (touchDark) nightWake();                 // the dark: wake, don't flip the page
      else if (!privacyLock) switchApp(dx < 0 ? 1 : -1, true);
      touchSwipeDone = true;     // one page per swipe
      touchMoved = true;
      touchPortalArmed = false;
    } else if (abs(dy) > 55 && abs(dy) > abs(dx)+18) {
      if (settings.night.enabled && nightActive) {
        // The whole night window, woken or not: back to the night level, so
        // the swipe shows what it changes.
        nightWoken = false;
        setNightBrightness(nightStep(settings.night.brightness, dy < 0));
        touchNightStepped = true;
      } else {
        setBrightness((int)brightnessLevel + (dy < 0 ? 24 : -24));
      }
      // Re-base so a continued drag steps brightness again every 55 px.
      touchStartX = x; touchStartY = y;
      touchMoved = true;
      touchPortalArmed = false;
    }
  } else if (touchWasDown) {
    // The AXS5106L INT/read path can miss the odd frame. Require a few
    // consecutive misses before treating it as release, otherwise taps/swipes
    // get chopped up and feel flaky.
    if (++touchMissFrames < 3 || millis() - touchSeenAt < 120UL) return;
    uint32_t pressMs = millis() - touchStartMs;
    int16_t dx = (int16_t)touchLastX-(int16_t)touchStartX;
    int16_t dy = (int16_t)touchLastY-(int16_t)touchStartY;
    if (touchDark) {
      // The dark: a press wakes, however long, and never counts as half a
      // double tap. A short up or down drag doesn't: it was a swipe that fell
      // short of a step, not a request to wake.
      const bool shortDrag = touchMoved && abs(dy) > abs(dx);
      if (pressMs >= 35 && !touchSwipeDone && !touchNightStepped && !shortDrag) {
        nightWake();
        tapCount = 0;
        lastTapMs = 0;
      }
    } else if (pressMs >= 35 && pressMs <= 650 && !touchMoved && abs(dx) < 35 && abs(dy) < 35) {
      triggerFaceTap();
    }
    touchWasDown = false;
    touchMissFrames = 0;
    touchMoved = false;
    touchSwipeDone = false;
    touchPortalArmed = false;
  }
}

// ── Knocks ───────────────────────────────────────────────
// Two or more knocks, 0.1 to 0.9 s apart, get a greeting and the Today page,
// or put away what plays over the page. knockTask() reads the pull a few hundred times a
// second, often enough to catch a knock: a jolt of over KNOCK_G against the
// slow average. A finger on the screen, a jolt over KNOCK_MAX_G or a shake
// means the Buddy is being handled, not knocked on.
#define KNOCK_G          0.25f   // a knock on its head reaches the Buddy at 0.35 to 2 g
#define KNOCK_MAX_G      6.0f    // a fall or a slam; a tap on the screen comes with a touch, a pick-up keeps moving
#define KNOCK_STILL_G    0.08f   // under this the Buddy is still
#define KNOCK_STILL_MS   50     // a knock rings on for up to 300 ms: a new one starts after this long still
#define KNOCK_EVERY_MS   3
#define KNOCK_GAP_MIN_MS 80
#define KNOCK_GAP_MAX_MS 700
#define KNOCK_LEAD_MS    150     // still this long before the first knock: typing never is
#define KNOCK_QUIET_MS   250     // the knocks are over after this long without one
#define KNOCK_ECHO       0.25f   // a later jolt under this share of the first knock is the Buddy rocking on its stand
volatile bool knockWatching = false;
volatile uint32_t knockCount = 0;   // double knocks so far
volatile float knockBiggest = 0;    // the largest jolt since the status line last showed it
volatile uint32_t knockStarts = 0;  // single knocks so far; the last 8 strengths in knockRing
volatile float knockRing[8] = {0};

static void knockTask(void *) {
  float average[3] = {0, 0, 0};
  bool primed = false;
  uint32_t first = 0, second = 0, stillSince = 0, handledAt = 0, tick = 0;
  float firstG = 0;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(KNOCK_EVERY_MS));
    xSemaphoreTake(i2cMutex, portMAX_DELAY);
    float axes[3];
    bool fresh = false;
    if (++tick % 23 == 0) imu.update();   // every 70 ms the full reading the loop uses
    else fresh = imu.pull(axes);
    xSemaphoreGive(i2cMutex);
    if (!fresh) continue;
    if (!primed) {
      for (int i = 0; i < 3; i++) average[i] = axes[i];
      primed = true;
      continue;
    }
    const uint32_t now = millis();
    // The jolt is the change in every direction: a knock from the side turns the
    // pull far more than it lengthens it.
    float squared = 0;
    for (int i = 0; i < 3; i++) {
      const float off = axes[i] - average[i];
      squared += off * off;
      average[i] += off * 0.02f;
    }
    const float jolt = sqrtf(squared);
    if (jolt > knockBiggest) knockBiggest = jolt;
    if (jolt > KNOCK_MAX_G) {   // handled, not knocked on
      handledAt = now;
      first = second = 0;
    }
    if (second && now - second > KNOCK_QUIET_MS) {   // two knocks or more, then quiet
      // No finger on the screen from just before the first knock on, no handling, no shake.
      if (millisSince(touchSeenAt) > now - first + 300UL && now - handledAt > 1000UL && !shakeAtMs) knockCount++;
      first = second = 0;
    }
    const uint32_t stillFor = now - stillSince;
    const bool knock = jolt >= KNOCK_G && jolt <= KNOCK_MAX_G && stillFor >= KNOCK_STILL_MS;
    if (jolt >= KNOCK_STILL_G) stillSince = now;   // still since the last jolt over KNOCK_STILL_G
    if (!knock) continue;
    knockRing[knockStarts % 8] = jolt;
    knockStarts++;
    if (first && !second && now - first > KNOCK_GAP_MAX_MS) first = 0;   // too late for a pair
    if (first && jolt < KNOCK_ECHO * firstG) continue;                    // an echo, no knock
    if (!first) {
      if (stillFor >= KNOCK_LEAD_MS) {
        first = now;
        firstG = jolt;
      }
    } else if (second && jolt >= 0.6f * firstG) {   // a third real knock: typing or handling, not a knock-knock
      first = second = 0;
    } else if (now - first >= KNOCK_GAP_MIN_MS) {
      second = now;
    }
  }
}

void knockStart() {
  i2cMutex = xSemaphoreCreateMutex();
  knockWatching = imuReady && i2cMutex
                  && xTaskCreate(knockTask, "knock", 3072, nullptr, 2, nullptr) == pdPASS;
  Serial.printf("knock watch %s\n", knockWatching ? "on" : "off");
}

// ── Night mode ────────────────────────────────────────────
// The only place that writes the backlight, so backlightOut stays true.
void writeBacklight(uint8_t level) {
  if (level == backlightOut) return;
  ledcWrite(LCD_BL, level);
  backlightOut = level;
}

// True while night mode has the screen dimmed. A tap lifts it for NIGHT_WAKE_MS.
bool nightDimming() {
  if (nightWoken && millis() - nightWokenAt >= NIGHT_WAKE_MS) nightWoken = false;
  return settings.night.enabled && nightActive && !nightWoken;
}

// The backlight level right now: the night level as the user set it (it may be
// above the day brightness), or the day brightness.
uint8_t backlightTarget() {
  return nightDimming() ? settings.night.brightness : brightnessLevel;
}

void nightWake() {
  if (!settings.night.enabled || !nightActive) return;
  nightWoken   = true;
  nightWokenAt = millis();
}

// A swipe at night: changes the level now, saved once the swipes settle.
void setNightBrightness(uint8_t level) {
  settings.night.brightness = level;
  nightSavePending = true;
  brightnessChangedMs = millis();
  brightnessBannerUntil = (millis() + 1200) | 1;
  nightBanner = true;
  Serial.printf("Night brightness: %u/255\n", (unsigned)level);
}

// Writes a pending night level once the swipes have settled: one write, never
// one per swipe, and none while the finger is down. Skips the write when the
// level ended where it started. settingsToJson writes every setting, so unknown
// keys in the file are dropped, as by a SET.
void flushNightSave(bool now) {
  if (!now && touchWasDown) return;
  if (!nightSavePending) return;
  if (configReplaced) { nightSavePending = false; return; }
  if (!now && millis() - brightnessChangedMs < BRIGHTNESS_SAVE_DELAY_MS) return;
  nightSavePending = false;
  if (settings.night.brightness == savedNightBrightness) return;
  settings.brightness = brightnessLevel;
  JsonDocument file;
  settingsToJson(settings, file.to<JsonObject>());
  if (file.overflowed()) { Serial.println("night: could not save"); return; }
  if (!writeJsonAtomic(CONFIG_PATH, file)) { Serial.println("night: could not save"); return; }
  savedNightBrightness = settings.night.brightness;
  settingsStored = true;                  // a source build now has a file
  settingsAdded.count = 0;                // the file has every key now
  Serial.printf("Night brightness saved: %u/255\n", (unsigned)settings.night.brightness);
}

void updateNightMode() {
  if (settings.night.enabled && clockTrusted) {
    const bool inWindow = inNightWindow(currentClockHour(), settings.night.start, settings.night.end);
    if (inWindow != nightActive) {
      nightActive = inWindow;
      nightWoken  = false;                                  // a wake ends with its night
      if (inWindow) faceMood = MOOD_SLEEPY;                 // a tap still changes it
      else if (faceMood == MOOD_SLEEPY) faceMood = MOOD_NORMAL;
      Serial.printf("Night mode: %s\n", inWindow ? "on" : "off");
    }
  }
  writeBacklight(backlightTarget());
}

static constexpr uint32_t TIMER_DONE_SHOW_MS = 15000UL;
static constexpr uint32_t SHAKE_DIZZY_MS     = 1300UL;   // still dizzy that long after the last jolt
static constexpr uint32_t SHAKE_LOOK_MS      = 2400UL;   // then the look, before the Today page
static constexpr uint32_t KNOCK_HELLO_MS     = 2000UL;   // the greeting for a double knock, before the Today page
static constexpr int32_t  MEETING_WARN_S     = 300;

// Starts or stops the timer. A session of a minute or more counts for the day.
void timerSet(bool run, bool byPc) {
  if (run == (timerStartMs != 0)) return;
  if (run) {
    timerStartMs  = millis() | 1;
    timerByPc     = byPc;
    timerDoneUntil = 0;
    Serial.printf("timer: started by %s\n", byPc ? "the PC" : "lying flat");
    return;
  }
  timerLastS     = secondsSince(timerStartMs);
  if (timerLastS >= 25 * 60) gifShow(GIF_FOCUS_DONE, 7000);
  timerStartMs   = 0;
  timerDoneUntil = (millis() + TIMER_DONE_SHOW_MS) | 1;
  if (timerLastS >= 60) {
    day.focusS += timerLastS;
    day.sessions++;
    daySave();
  }
}

// Lying flat (screen up or down) for 2 s starts the timer once; standing the
// Buddy up stops one that began that way.
static void updateTimer() {
  static uint32_t flatSince = 0;
  static bool flatUsed = false, wasLocked = false;
  const bool flat = imuReady && fabs(filteredAz) > 0.8f;
  if (!flat) {
    flatSince = 0;
    flatUsed = false;
    if (timerStartMs && !timerByPc) timerSet(false, false);
  } else {
    if (flatSince == 0) flatSince = millis() | 1;
    if (millisSince(flatSince) > 2000UL) {
      if (!flatUsed && !timerStartMs) timerSet(true, false);
      flatUsed = true;
    }
  }
  const bool locked = pc.known && pc.locked;
  if (locked && !wasLocked && timerStartMs) timerSet(false, false);
  wasLocked = locked;
  if (timerDoneUntil && timeReached(millis(), timerDoneUntil)) timerDoneUntil = 0;
}

static void timerText(const char *word, uint32_t seconds, char *out, size_t len) {
  if (seconds >= 3600) {
    snprintf(out, len, "%s %lu:%02lu:%02lu", word, (unsigned long)(seconds / 3600), (unsigned long)(seconds / 60 % 60), (unsigned long)(seconds % 60));
  } else {
    snprintf(out, len, "%s %lu:%02lu", word, (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  }
}

// The Buddy's mood follows the day. First match wins:
//   a double knock   surprised eyes and a greeting, then the Today page
//   a shake          dizzy while it lasts, then a flat look, then the Today page
//   night            night mode's own sleepy face
//   microphone on    the ON AIR sign and how long it has been on
//   a meeting soon   MEETING IN n MIN from 5 minutes before, with its name
//   the timer        FOCUS mm:ss counting up, then FOCUSED mm:ss for a moment
//   PC locked        LUNCH BREAK from 12 to 14, else AWAY
//   Claude Code      CLAUDE NEEDS YOU, with the project
//   a weekday        LOG TUESDAY'S HOURS while under half of the working day before is logged
//   a PR check red   CI RED
//   Claude Code      CLAUDE DONE, with the project
//   a task in dev    LOCKED IN
// The mood is set when the status changes, so a tap on the face still cycles
// moods until the next change.
// The personal settings and the time, as lib/persona reads them.
static PersonaPrefs personaPrefs() {
  return PersonaPrefs{settings.name, settings.birthday, settings.since, settings.message, settings.breakMin,
                      settings.water, settings.work.start, settings.work.end, settings.work.lunch, settings.weekend};
}

void updateBuddyMood() {
  static uint8_t lastStatus = 0;
  static char text[28], other[27];   // other: at most the 26 characters a caption has room for
  updateTimer();
  dayRoll();
  if (joinShownUntil && timeReached(millis(), joinShownUntil)) joinShownUntil = 0;
  if (netView.odoo.valid && millisSince(netView.odoo.updatedAt) > ODOO_STALE_MS) {   // the PC stopped sending
    const OdooData none = {};
    publishNetData(netShared.odoo, none);
  }
  if (pc.known && millis() - pc.at > PC_STATE_MAX_AGE_MS) pc.known = false;
  const bool onAir = pc.known && pc.mic;
  if (onAir && !micSinceMs) micSinceMs = millis() | 1;
  if (!onAir && micSinceMs) {   // a meeting of a minute or more counts for the day
    const uint32_t seconds = secondsSince(micSinceMs);
    micSinceMs = 0;
    if (seconds >= 60) { day.meetS += seconds; daySave(); }
  }
  const LocalNow now = localNow();
  const OdooData &odoo = netView.odoo;
  const int64_t utc = (int64_t)time(nullptr);
  const OdooMeeting *soon = nullptr;   // starts within MEETING_WARN_S, or began under a minute ago
  for (uint8_t i = 0; clockTrusted && i < odoo.meetingCount && !soon; i++) {
    const int64_t ahead = odoo.meetings[i].start - utc;
    if (ahead <= MEETING_WARN_S && ahead > -60) soon = &odoo.meetings[i];
  }
  uint8_t status = 0, mood = MOOD_NORMAL;
  const char *caption = nullptr, *small = nullptr;
  const uint8_t demo = pc.known ? pc.demo : 0;
  // Personal touches (lib/persona): statuses 17 to 24, drawn with the faces there are.
  //   17 birthday, 18 anniversary, 19 stretch, 20 water, 21 lunch time, 22 weekend, 23 welcome back, 24 message
  const PersonaPrefs prefs = personaPrefs();
  const PersonaNow pnow = {(int)now.year, now.month, now.mday, now.wday, now.hour, now.min, now.sec, clockTrusted};
  const PersonaStatus nudge = demo ? PERSONA_NONE : personaNudge(prefs, pnow);
  static char personal[PERSONA_CAPTION];
  // Shows a persona status with its face; the birthday adds the owner's name as the second line.
  auto show = [&](PersonaStatus what) {
    static const uint8_t NUMBER[] = {0, 17, 18, 19, 21, 20, 24, 22};
    static const uint8_t FACE[]   = {MOOD_NORMAL, MOOD_PARTY, MOOD_LOVE, MOOD_HAPPY, MOOD_HAPPY, MOOD_HAPPY, MOOD_HAPPY, MOOD_SLEEPY};
    personaCaption(what, prefs, pnow, personal, sizeof(personal));
    if (what == PERSONA_ANNIVERSARY && !personal[0]) snprintf(personal, sizeof(personal), "1 YEAR TOGETHER");   // demo
    if (what == PERSONA_MESSAGE && !personal[0]) snprintf(personal, sizeof(personal), "You've got this");       // demo
    status = NUMBER[what]; mood = FACE[what]; caption = personal;
    if (what == PERSONA_BIRTHDAY && settings.name[0]) small = settings.name;
  };
  // Back after ten minutes or more away: a surprised hello for 3 s.
  static uint32_t lockedSince = 0, welcomeUntil = 0;
  if (pc.known && pc.locked && !demo) {
    if (!lockedSince) lockedSince = millis() | 1;
  } else if (lockedSince) {
    if (pc.known && !demo && millisSince(lockedSince) >= 10UL * 60UL * 1000UL) welcomeUntil = (millis() + 3000) | 1;
    lockedSince = 0;
  }
  if (welcomeUntil && timeReached(millis(), welcomeUntil)) welcomeUntil = 0;
  const uint32_t shaken = shakeAtMs ? millisSince(shakeAtMs) : 0;
  if (shakeAtMs && shaken >= SHAKE_DIZZY_MS + SHAKE_LOOK_MS) {
    shakeAtMs = 0;
    if (!privacyLock) todayShow("TODAY", 12000);
  }
  if (knockAtMs && millisSince(knockAtMs) >= KNOCK_HELLO_MS) {
    knockAtMs = 0;
    if (!privacyLock) todayShow("TODAY", 12000);
  }
  if (waveUntil && timeReached(millis(), waveUntil)) waveUntil = 0;
  static char meetMe[PERSONA_CAPTION], meetYou[PERSONA_CAPTION];
  if (demo == 25 || waveUntil) {   // 25: the hello at the end of setup, with a wave
    personaMeet(settings.buddyName, settings.name, meetMe, sizeof(meetMe), meetYou, sizeof(meetYou));
    status = 25; mood = MOOD_HAPPY; caption = meetMe; small = meetYou[0] ? meetYou : nullptr;
  } else if (demo == 15 || (!demo && knockAtMs)) {
    status = 15; mood = MOOD_SURPRISED; caption = demo ? "WHO'S THERE?" : knockHello;
  } else if (demo == 13 || (!demo && shakeAtMs && shaken < SHAKE_DIZZY_MS)) {
    status = 13; mood = MOOD_DIZZY; caption = "WHOAAA";
  } else if (demo == 14 || (!demo && shakeAtMs)) {
    status = 14; mood = MOOD_UNIMPRESSED; caption = "REALLY?";
  } else if (demo == 23 || (!demo && welcomeUntil)) {
    personaWelcome(settings.name, personal, sizeof(personal));
    status = 23; mood = MOOD_SURPRISED; caption = personal;
  } else if (demo == 1 || (!demo && nightActive)) {
    status = 1; mood = MOOD_SLEEPY;
  } else if (demo == 11 || (!demo && onAir)) {
    timerText("IN A MEETING", demo ? 754 : secondsSince(micSinceMs), text, sizeof(text));
    status = 11; mood = MOOD_ONAIR; caption = text;
  } else if (demo == 12 || demo == 16 || (!demo && soon)) {
    const long ahead = demo ? 240 : (long)(soon->start - utc);
    if (ahead >= 60) snprintf(text, sizeof(text), "MEETING IN %ld MIN", (ahead + 59) / 60);
    else             snprintf(text, sizeof(text), "MEETING NOW");
    status = 12; mood = MOOD_SURPRISED; caption = text; small = demo ? "Weekly sync" : soon->name;
    if (demo == 16 || joinShownUntil) { caption = "OPENING THE LINK"; small = nullptr; }   // a tap asked to join
  } else if (demo == 19 || (!demo && timerStartMs != 0 && personaStretchDue(settings.breakMin, secondsSince(timerStartMs)))) {
    show(PERSONA_STRETCH);   // the timer runs on
  } else if (demo == 7 || (!demo && timerStartMs != 0)) {
    timerText("FOCUS", demo ? 754 : secondsSince(timerStartMs), text, sizeof(text));
    status = 7; mood = MOOD_FOCUS; caption = text;
  } else if (demo == 8 || (!demo && timerDoneUntil != 0)) {
    timerText("FOCUSED", demo ? 2830 : timerLastS, text, sizeof(text));
    status = 8; mood = MOOD_EXCITED; caption = text;
  } else if (demo == 2 || demo == 3 || (!demo && pc.known && pc.locked)) {
    const bool lunch = demo == 2 || (!demo && clockTrusted && now.hour >= settings.work.lunch && now.hour < settings.work.lunch + 2);
    status = lunch ? 2 : 3; mood = lunch ? MOOD_LUNCH : MOOD_AWAY;
    caption = lunch ? "LUNCH BREAK" : "AWAY";
  } else if (demo == 9 || (!demo && pc.known && pc.claude == CLAUDE_NEEDS_YOU)) {
    status = 9; mood = MOOD_SURPRISED; caption = "CLAUDE NEEDS YOU"; small = pc.project;
  } else if (demo == 21 || nudge == PERSONA_LUNCH) {
    show(PERSONA_LUNCH);
  } else if (demo == 20 || nudge == PERSONA_WATER) {
    show(PERSONA_WATER);
  } else if (nudge == PERSONA_BIRTHDAY || nudge == PERSONA_ANNIVERSARY) {
    show(nudge);
  } else if (demo == 4 || (!demo && clockTrusted && odoo.valid && personaWorkDay(settings.weekend, now.wday)
             && odoo.hoursBefore < ODOO_DAY_HOURS / 2.0f)) {
    snprintf(text, sizeof(text), "LOG %s'S HOURS", DAY_NAMES[demo ? 2 : odoo.beforeWday % 7]);
    status = 4; mood = MOOD_NUDGE; caption = text;
  } else if (demo == 5 || (!demo && pc.known && pc.prsFailing > 0)) {
    snprintf(text, sizeof(text), "CI RED: %d", pc.prsFailing > 0 ? pc.prsFailing : 1);
    status = 5; mood = MOOD_CI; caption = text;
  } else if (demo == 10 || (!demo && pc.known && pc.claude == CLAUDE_DONE)) {
    status = 10; mood = MOOD_HAPPY; caption = "CLAUDE DONE"; small = pc.project;
  } else if (demo == 6 || (!demo && odoo.valid && odoo.dev > 0)) {
    status = 6; mood = MOOD_FOCUS; caption = "LOCKED IN";
  } else if (demo == 17 || demo == 18 || demo == 22 || demo == 24) {
    show(demo == 17 ? PERSONA_BIRTHDAY : demo == 18 ? PERSONA_ANNIVERSARY : demo == 22 ? PERSONA_WEEKEND : PERSONA_MESSAGE);
  } else if (!demo) {
    const PersonaStatus idle = personaIdle(prefs, pnow);   // nothing else to say
    if (idle != PERSONA_NONE) show(idle);
  }
  // Away for 30 minutes: the screensaver, until the PC is unlocked.
  static uint32_t awaySince = 0;
  if (status == 3 && !demo) {
    if (!awaySince) awaySince = millis() | 1;
    if (millisSince(awaySince) > 30UL * 60UL * 1000UL && !gifPlayUntil) gifShow(SCREENSAVER, 60000);
  } else {
    awaySince = 0;
    if (gifPlayUntil && strcmp(gifPlay, SCREENSAVER) == 0) gifPlayUntil = 1;
  }
  faceWave    = status == 25;
  faceCaption = caption;
  odooAscii(small ? small : "", other, sizeof(other), sizeof(other) - 1);
  faceSub     = other[0] ? other : nullptr;
  faceStatus  = demo ? 0 : status;
  faceHold    = !demo && (status == 2 || status == 3 || status == 7 || status == 8 || status == 9
                          || (status >= 11 && status <= 16) || status == 19 || status == 23 || status == 25);
  if (status != lastStatus) {
    if ((status == 13 || status == 15 || status == 25) && pageOrder[0] == PAGE_FACE) {   // a shake or a knock turns to the face from any page
      currentPageIdx = 0;
      currentApp = PAGE_FACE;
    }
    lastStatus = status;
    faceMood   = mood;
    pressPulse = 1.0f;
    Serial.printf("mood: status %u%s%s\n", (unsigned)status, caption ? " " : "", caption ? caption : "");
  }
}

void updateFaceTimers() {
  uint32_t now = millis();
  if (timeReached(now, nextBlink)) {
    blinkUntil = now + (random(0,6)==0 ? 220 : 105);
    nextBlink  = now + 1000 + random(0, 2600);
  }
  if (timeReached(now, nextGlance)) {
    faceTargetX = (float)random(-8, 9);
    faceTargetY = (float)random(-4, 5);
    nextGlance  = now + 650 + random(0, 1500);
  }
  faceGlanceX = faceGlanceX*0.84f + faceTargetX*0.16f;
  faceGlanceY = faceGlanceY*0.84f + faceTargetY*0.16f;
  pressPulse *= 0.86f;
}

void updateAutoPage() {
  // faceHold (PC locked, focus timer, Claude waiting): go to the face page,
  // which says why, and stay there. A swipe still changes page.
  static bool held = false;
  if (todayUntil) {   // a briefing (Yesterday first), the recap or a shake: the Today page, then back to the first page
    if (!timeReached(millis(), todayUntil)) {
      if (yesterdayUntil && timeReached(millis(), yesterdayUntil)) yesterdayUntil = 0;
      currentApp = yesterdayUntil ? PAGE_YESTERDAY : PAGE_TODAY;
      nextAutoPage = millis() + PAGE_AUTO_INTERVAL_MS;
      return;
    }
    todayUntil = 0;
    todayTitle = "TODAY";
    currentPageIdx = 0;
    currentApp = pageOrder[0];
  }
  const bool hold = faceHold || privacyLock;
  if (hold) {
    if (!held && pageOrder[0] == PAGE_FACE) { currentPageIdx = 0; currentApp = PAGE_FACE; }
    nextAutoPage = millis() + PAGE_AUTO_INTERVAL_MS;
  } else if (autoPageEnabled && timeReached(millis(), nextAutoPage)) {
    switchApp(1);
  }
  held = hold;
}

// Returns the current local hour (0-23).
static uint8_t currentClockHour() {
  return localNow().hour;
}

// ── Background fetch task ────────────────────────────────
// Owns the fetch schedule so loop() never waits on the network:
//   stock   the stock on screen, when its quote is STOCK_REFRESH_MS old
//   weather prefetched every WEATHER_REFRESH_INTERVAL_MS
//   GitHub  prefetched every GITHUB_REFRESH_INTERVAL_MS
// A failed fetch backs off 10 s, 20 s, 40 s, ... (see FETCH_RETRY_MIN_MS). At
// most one fetch per pass, so only one HTTPS connection exists at a time.

// Retry pacing for one data source.
struct FetchPacer {
  uint32_t attemptedAt = 0;                    // 0 = never attempted
  uint32_t waitMs      = 0;                    // wait after attemptedAt before the next try
  uint32_t failWaitMs  = FETCH_RETRY_MIN_MS;   // wait to use after the next failure

  bool ready(uint32_t now) const { return attemptedAt == 0 || now - attemptedAt >= waitMs; }

  // okWaitMs: wait after a success; maxFailWaitMs: backoff ceiling.
  void record(uint32_t now, bool ok, uint32_t okWaitMs, uint32_t maxFailWaitMs) {
    attemptedAt = now;
    if (ok) {
      waitMs     = okWaitMs;
      failWaitMs = FETCH_RETRY_MIN_MS;
    } else {
      waitMs     = failWaitMs;
      failWaitMs = (failWaitMs * 2 > maxFailWaitMs) ? maxFailWaitMs : failWaitMs * 2;
    }
  }
};

// Run one fetch, update its pacer and log the result, e.g.
// "[net] weather ok 640 ms" or "[net] weather FAIL 4005 ms, retry in 10 s".
static void runFetch(const char *name, bool (*fetch)(), FetchPacer &pacer, uint32_t now,
                     uint32_t okWaitMs, uint32_t maxFailWaitMs) {
  bool ok = fetch();
  uint32_t tookMs = millis() - now;
  pacer.record(now, ok, okWaitMs, maxFailWaitMs);
  if (ok) Serial.printf("[net] %s ok %lu ms\n", name, (unsigned long)tookMs);
  else    Serial.printf("[net] %s FAIL %lu ms, retry in %lu s\n", name, (unsigned long)tookMs,
                        (unsigned long)(pacer.waitMs / 1000UL));
}

// Free heap, lowest free heap since boot, and this task's unused stack.
static void logNetHealth() {
  Serial.printf("[net] heap %luk (min %luk), stack free %lu bytes\n",
                (unsigned long)(ESP.getFreeHeap() / 1024),
                (unsigned long)(ESP.getMinFreeHeap() / 1024),
                (unsigned long)uxTaskGetStackHighWaterMark(nullptr));
}

void netTask(void *) {
  FetchPacer weatherPacer, stockPacer, githubPacer, finnhubCoinPacer, coinbasePacer;
  uint32_t lastHealthLogMs = millis();
  bool tlsHealthLogged = false;

  for (;;) {
    if (!ensureWifi()) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (ntpStartWanted) {
      ntpStartWanted = false;
      startNtp();
    }

    // This task is netShared's only writer, so it may read it without the lock.
    const uint32_t now = millis();
    bool ran = false, usedTls = false;
    // Stock is paced by each stock's own age (okWaitMs 0), weather and GitHub by data age; all back off after a failure.
    // The stock on screen, when its own quote is a minute old (or missing).
    const uint8_t slot = shownStock();
    const StockData &shownQuote = netShared.stocks[slot];
    const bool stockDue = settings.pages[PAGE_STOCK] && stockConfigured() && currentApp == PAGE_STOCK
        && (!shownQuote.valid || now - shownQuote.updatedAt >= STOCK_REFRESH_MS) && stockPacer.ready(now);
    const uint8_t cslot = shownCoin();
    const CoinData &cq = netShared.coins[cslot];
    const bool coinFinnhub = settings.coins[cslot].source == COIN_FINNHUB;
    FetchPacer &coinPacer = coinFinnhub ? finnhubCoinPacer : coinbasePacer;
    const bool coinDue = settings.pages[PAGE_COIN] && coinConfigured() && currentApp == PAGE_COIN
        && !cq.notFound && (!cq.valid || now - cq.updatedAt >= COIN_REFRESH_MS) && coinPacer.ready(now);
    if (coinDue) {
      coinFetchIdx = cslot;
      runFetch("coin", fetchCoin, coinPacer, now, 0, FETCH_RETRY_MS);
      ran = usedTls = true;
    } else if (stockDue) {
      stockFetchIdx = slot;
      runFetch("stock", fetchStock, stockPacer, now, 0, FETCH_RETRY_MS);
      ran = usedTls = true;
    } else if (settings.pages[PAGE_WEATHER]
        && (!netShared.weather.valid || now - netShared.weather.updatedAt >= WEATHER_REFRESH_INTERVAL_MS)
        && weatherPacer.ready(now)) {
      runFetch("weather", fetchWeather, weatherPacer, now, 0, FETCH_RETRY_MS);
      ran = usedTls = true;
    } else if (settings.pages[PAGE_GITHUB] && githubConfigured()
        && (!netShared.github.valid || !netShared.github.haveStars || !netShared.github.haveActivity
            || now - netShared.github.updatedAt >= GITHUB_REFRESH_INTERVAL_MS)
        && githubPacer.ready(now)) {
      runFetch("github", fetchGithub, githubPacer, now, 0, GITHUB_RETRY_MAX_MS);
      ran = usedTls = true;
    }

    // Stack use peaks during the first HTTPS handshake; log then, and every 5 min.
    if ((usedTls && !tlsHealthLogged) || millis() - lastHealthLogMs >= NET_HEALTH_LOG_MS) {
      logNetHealth();
      lastHealthLogMs = millis();
      if (usedTls) tlsHealthLogged = true;
    }
    vTaskDelay(pdMS_TO_TICKS(ran ? 250 : 500));
  }
}

void calibrateNeutral() {
  gfx->fillScreen(BG);
  centeredText("HOLD STILL", 76, 2);
  gfx->flush();
  delay(900);
  for (uint8_t i = 0; i < 100; i++) { imu.update(); delay(5); }
  float sumX=0.0f, sumY=0.0f;
  for (uint8_t i = 0; i < 140; i++) {
    imu.update(); imu.getAccel(&accel);
    sumX += accel.accelX; sumY += accel.accelY;
    delay(5);
  }
  restAx = sumX/140.0f; restAy = sumY/140.0f;
  filteredAx = restAx;  filteredAy = restAy;
}

// ── Arduino entry points ──────────────────────────────────

void setup() {
  // TX: room for a whole protocol line (2 KB), so a GET reply is never cut
  // short by setTxTimeoutMs(0) below. RX: twice that, so an over-long line
  // that arrives in one burst still fits whole, newline included, and gets
  // its "line too long" reply instead of being truncated by the driver.
  // Both must be set before begin().
  Serial.setRxBufferSize(2 * LineReader::CAPACITY);
  Serial.setTxBufferSize(LineReader::CAPACITY);
  Serial.begin(115200);
  pinMode(SD_CS, OUTPUT);       // the card must stay silent while the display talks
  digitalWrite(SD_CS, HIGH);
  // Never wait on the USB serial. Once a serial monitor has read the port,
  // the core treats it as connected; when the monitor closes, every print
  // then blocks for up to 2 s, slowing the loop to a crawl (no blinking,
  // touch ignored) until the next reset. With no wait, output nobody reads
  // is simply dropped.
  Serial.setTxTimeoutMs(0);
  delay(150);
  Serial.println("ESP32-C6 Monitor-Buddy " FW_VERSION " starting");
  pinMode(BOOT_PIN, INPUT_PULLUP);
  netMutex = xSemaphoreCreateMutex();
  if (!netMutex) Serial.println("ERROR: no memory for netMutex - network pages disabled");

  // Settings first: the mounting side decides the rotation below. AWM's
  // begin() mounts LittleFS again later; that is a no-op once mounted.
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed - using config.h values");
  loadSettings();
  loadKeys();
  savedNightBrightness = settings.night.brightness;
  screenRotation = settings.mount == MOUNT_RIGHT ? 3 : 1;
  tiltSign       = settings.mount == MOUNT_RIGHT ? -1.0f : 1.0f;
  setenv("TZ", settings.tzRule, 1);
  tzset();
  applyTheme();   // themed colours from settings.theme

  if (!gfx->begin(40000000)) Serial.println("Display init failed — check wiring");
  lcdRegInit();
  display->setRotation(screenRotation);
  ledcAttach(LCD_BL, 5000, 8);
  writeBacklight(brightnessLevel);
  gfx->fillScreen(BG);
  gfx->flush();

  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);   // the panel and the motion sensor both take fast mode
  bsp_touch_init(&Wire, TOUCH_RST, TOUCH_INT, screenRotation, gfx->width(), gfx->height());
  touchReady = true;
  Serial.println("Touch controller initialised");

  int err = imu.init(calib, IMU_ADDRESS);
  if (err == 0) {
    imuReady = (imu.setAccelRange(4)==0 && imu.setGyroRange(512)==0);
    if (imuReady) calibrateNeutral();
  }
  knockStart();
  if (!imuReady) Serial.println("IMU unavailable — using animated fallback motion");

  randomSeed(micros());
  // Initialise the DESKBUDDY-style eyes, centred on the face page.
  leftEye.init(FACE_LEFT_CX,  FACE_EYES_CY, 90, 90);
  rightEye.init(FACE_RIGHT_CX, FACE_EYES_CY, 90, 90);

  // Build the enabled-page list from settings.pages.
  buildStockList();
  buildCoinList();
  buildPageList();
  // Seed the clock from the compile time as a safe fallback. syncNTP()
  // replaces it with the real time once Wi-Fi is up.
  clockStartMillis = millis();
  seedClockFromCompileTime();
  nextBlink         = millis() + 1200;
  nextGlance        = millis() + 600;
  nextAutoPage      = millis() + PAGE_AUTO_INTERVAL_MS;

  // ── AyresWiFiManager setup ──────────────────────────────
  // Order matters: all setters first, then begin() (mounts LittleFS and
  // loads /wifi.json), then run() (connects, or applies the fallback).
  drawBootMessage(settings.buddyName[0] ? settings.buddyName : "Monitor-Buddy",
                  settings.buddyName[0] ? "is waking up..." : "starting up...", nullptr);

  wifiManager.setHostname(AWM_HOSTNAME);
  wifiManager.setAPCredentials(AP_SSID, AP_PASS);
  wifiManager.setCaptivePortal(true);            // DNS catch-all → auto-popup portal
  wifiManager.setPortalTimeout(PORTAL_TIMEOUT_S);
  wifiManager.setAPClientCheck(true);            // don't close while a phone is joined
  wifiManager.setWebClientCheck(true);           // every HTTP request resets the timer

  // The portal opens by itself only with no Wi-Fi stored. With one stored, a
  // failed connect is retried (maintainWifi()): a router that is slow after a
  // power cut must not leave an open setup hotspot. A password that changed
  // is fixed by holding the screen.
  wifiManager.setFallbackPolicy(AyresWiFiManager::FallbackPolicy::NO_CREDENTIALS_ONLY);
  wifiManager.setAutoReconnect(true);
  wifiManager.setLedAuto(true);

  wifiManager.setReconnectAttemptMs(WIFI_RETRY_WINDOW_MS);
  wifiManager.setReconnectBackoffMs(WIFI_RETRY_INTERVAL_MS);

  // syncNTP() owns the clock and the timezone; AWM stays out of it.
  wifiManager.setTimeSync(false);

  // Note: deliberately NOT calling setProtectedJsons().
  // Protecting /wifi.json would make the portal's "erase credentials"
  // button do nothing. Protecting /display.json would keep a near-black
  // brightness across the portal's erase, which is the case that reset is
  // meant to recover.

  if (LittleFS.begin(true)) chooseSavedWifi();   // before AWM reads /wifi.json
  gifScan(SD.begin(SD_CS, SPI, SD_HZ));
  dayLoad();
  wifiManager.begin();   // mounts LittleFS, loads stored credentials
  loadDisplaySettings();
  savedBrightness = brightnessLevel;
  settings.brightness = brightnessLevel;
  writeBacklight(brightnessLevel);

  // Firmware up to 1.0.2 uploaded its own English-only setup pages to
  // LittleFS. AWM serves those in preference to its built-in, multilingual
  // pages, so remove them. /wifi.json and /display.json stay.
  for (const char *page : {"/index.html", "/success.html", "/error.html"}) {
    if (LittleFS.exists(page) && LittleFS.remove(page)) {
      Serial.printf("Removed old setup page %s\n", page);
    }
  }

  if (wifiManager.tieneCredenciales()) {
    drawBootMessage("CONNECTING", "to saved WiFi...", nullptr);
  } else {
    drawBootMessage("WIFI SETUP", "no saved network", "starting portal...");
  }

  // Blocks: up to 2 s button window, then up to 15 s connect attempt.
  // On failure it opens the captive portal per the ON_FAIL policy above.
  wifiManager.run();

  if (wifiManager.isConnected()) {
    Serial.printf("Wi-Fi connected. IP: %s  RSSI: %d\n",
                  WiFi.localIP().toString().c_str(), wifiManager.getSignalStrength());
    syncNTP();
  } else if (wifiManager.isPortalActive()) {
    Serial.println("Captive portal open — join " AP_SSID " and browse to 192.168.4.1");
  } else {
    Serial.println("Wi-Fi unavailable — running offline, hold the screen to configure");
  }

  // Start the fetch task last, once Wi-Fi state exists. It idles while the
  // portal is open or Wi-Fi is down.
  if (!netMutex) {
    Serial.println("ERROR: no netMutex - network pages disabled");
  } else if (xTaskCreate(netTask, "net", NET_TASK_STACK_BYTES, nullptr,
                         NET_TASK_PRIORITY, nullptr) != pdPASS) {
    Serial.println("ERROR: could not start fetch task - network pages disabled");
  }
  // A Buddy with a name of its own says so before the face comes up.
  if (settings.buddyName[0] && !wifiManager.isPortalActive()) {
    drawBootMessage(settings.buddyName, "is ready!", nullptr);
    delay(1200);
  }
}

static uint32_t statusFrames = 0, statusFramesAt = 0;   // frames drawn since the last status line

void loop() {
  // The web flasher's serial protocol. First, so it also answers while the
  // setup hotspot is open.
  serviceProtocol();

  // ── Captive-portal mode takes over the whole loop ───────
  // AWM must service DNS + HTTP promptly, and the network pages are
  // meaningless while the radio is in AP mode.
  if (wifiManager.isPortalActive()) {
    // Save now, long before anyone can press "erase" in the portal, so a
    // factory reset still removes /display.json.
    flushBrightnessSave(true);
    flushNightSave(true);
    writeBacklight(brightnessLevel);      // never run setup at night brightness
    servicePortal();
    return;
  }

  // Normal operation. update() still runs so the LED state machine and
  // any late portal shutdown are handled.
  // A run of the game skips what a frame can do without: the Wi-Fi upkeep,
  // 10 ms a call, runs twice a second and the motion sensor rests.
  // The Wi-Fi upkeep takes 10 ms a call: twice a second is plenty. The motion
  // sensor keeps the pace its filters and the shake were tuned at.
  static uint32_t upkeepAt = 0;
  const bool gameOn = currentApp == PAGE_GAME && gameRunning();
  if (millis() - upkeepAt >= 500UL) {
    upkeepAt = millis();
    wifiManager.update();
    maintainWifi();
  }
  serviceLink();

  static uint32_t sensedAt = 0;
  if (!gameOn && millis() - sensedAt >= 70UL) {
    sensedAt = millis();
    readSensors();
  }
  readTouch();
  readBootButton();
  if (joinAskedAt && millisSince(joinAskedAt) > LINK_ASK_MS) joinAskedAt = 0;   // buddy_link.py did not come for it
  if (lockAskedAt && millisSince(lockAskedAt) > LINK_ASK_MS) lockAskedAt = 0;
  flushBrightnessSave(false);
  flushNightSave(false);

  // A touch-hold inside readTouch() may have just opened the portal.
  if (wifiManager.isPortalActive()) return;

  static uint32_t startsSeen = 0;
  if (knockStarts != startsSeen) {
    startsSeen = knockStarts;
    Serial.printf("knock: %.2f g\n", knockRing[(startsSeen - 1) % 8]);
    if (!privacyLock) leftEye.nextBlinkTime = millis();   // a blink: heard you
  }
  static uint32_t knocksSeen = 0;
  if (knockCount != knocksSeen) {
    knocksSeen = knockCount;
    Serial.println("knock: two knocks");
    if (noticeUntil) noticeUntil = 0;
    else if (gifPlayUntil) gifPlayUntil = 1;
    else if (!privacyLock) {   // a greeting, then the Today page (updateBuddyMood)
      static char hello[PERSONA_CAPTION];   // the owner's greeting style and name (lib/persona)
      personaGreeting(settings.greeting, settings.greetingText, settings.name, (uint32_t)random(0, 30), hello, sizeof(hello));
      knockHello = hello;
      knockAtMs = millis() | 1;
    }
  }
  updateAutoPage();
  updateFaceTimers();
  updateNightMode();
  updateBuddyMood();

  // Retry NTP if Wi-Fi came up after boot, and resync every 6 h to
  // correct drift and to undo any UTC-only resync done by AWM.
  if (ntpSynced && millis() - lastNtpSyncMs > 6UL * 3600UL * 1000UL) ntpSynced = false;
  if (!ntpSynced) syncNTP();


  float tx=0.0f, ty=0.0f;
  if (imuReady) {
    tx = clampFloat(-tiltSign*(filteredAy-restAy)*2.2f, -1.0f, 1.0f);
    ty = clampFloat( tiltSign*(filteredAx-restAx)*2.2f, -1.0f, 1.0f);
  } else {
    tx = sin(millis()*0.0014f)*0.25f;
    ty = cos(millis()*0.0011f)*0.16f;
  }

  snapshotNetData();
  if (noticeUntil && timeReached(millis(), noticeUntil)) noticeUntil = 0;
  if (gifPlayUntil && timeReached(millis(), gifPlayUntil)) {
    gifPlayUntil = 0;
    gifStop();
  }
  if ((currentApp != PAGE_GIFS && !gifPlayUntil) || noticeUntil) gifStop();   // anything else draws over the frame a GIF builds on
  if (noticeUntil) {
    drawNotice();
  } else if (gifPlayUntil && strcmp(gifPlay, SCREENSAVER) == 0) {
    drawBounce();
  } else if (gifPlayUntil) {
    drawGifs();
  } else switch (currentApp) {
    case 0: drawFace(tx, ty); break;
    case 1: drawClock();      break;
    case 2: drawDatePage();   break;
    case 3: drawWeather();    break;
    case 4: drawMoon();       break;
    case 5: drawStock();      break;
    case 6: drawGithub();     break;  // PAGE_GITHUB
    case 7: drawCoin();       break;  // PAGE_COIN
    case 8: drawAbda();       break;  // PAGE_ABDA
    case 9: drawOdoo();       break;  // PAGE_ODOO
    case 10: drawPrs();       break;  // PAGE_PRS
    case 11: drawGifs();      break;  // PAGE_GIFS
    case 12: drawToday();     break;  // PAGE_TODAY
    case 13: drawGame();      break;  // PAGE_GAME
    case 14: drawClaude();    break;  // PAGE_CLAUDE
    case 15: drawYesterday(); break;  // PAGE_YESTERDAY
    default: drawFace(tx, ty); break;
  }
  // Brightness or auto-advance feedback banner — shown briefly after a gesture.
  // A deadline is cleared once it passes, so it never goes stale long enough
  // for the signed comparison in timeReached() to wrap.
  if (brightnessBannerUntil && timeReached(millis(), brightnessBannerUntil)) brightnessBannerUntil = 0;
  if (autoPageBannerUntil && timeReached(millis(), autoPageBannerUntil)) autoPageBannerUntil = 0;
  if (brightnessBannerUntil) {
    char msg[20];
    if (nightBanner) snprintf(msg, sizeof(msg), "NIGHT %u%%", (unsigned)levelPercent(settings.night.brightness));
    else             snprintf(msg, sizeof(msg), "BRIGHTNESS %u%%", (unsigned)levelPercent(brightnessLevel));
    uint16_t bannerColor = rgb(0,200,255);
    gfx->setTextSize(1);
    gfx->setTextColor(bannerColor);
    int bw = (int)strlen(msg)*6;
    gfx->fillRoundRect((SCREEN_W-bw-12)/2, SCREEN_H/2-10, bw+12, 20, 4, BG);
    gfx->drawRoundRect((SCREEN_W-bw-12)/2, SCREEN_H/2-10, bw+12, 20, 4, bannerColor);
    gfx->setCursor((SCREEN_W-bw)/2, SCREEN_H/2-4);
    gfx->print(msg);
  } else if (autoPageBannerUntil) {
    const char *msg = autoPageEnabled ? "AUTO: ON" : "AUTO: OFF";
    uint16_t bannerColor = autoPageEnabled ? rgb(0,200,80) : rgb(200,60,60);
    gfx->setTextSize(1);
    gfx->setTextColor(bannerColor);
    int bw = (int)strlen(msg)*6;
    gfx->fillRoundRect((SCREEN_W-bw-12)/2, SCREEN_H/2-10, bw+12, 20, 4, BG);
    gfx->drawRoundRect((SCREEN_W-bw-12)/2, SCREEN_H/2-10, bw+12, 20, 4, bannerColor);
    gfx->setCursor((SCREEN_W-bw)/2, SCREEN_H/2-4);
    gfx->print(msg);
  }
  static bool gameShown = false;   // the first frame of a run goes out whole
  const bool gameShows = currentApp == PAGE_GAME && gameRunning() && !noticeUntil;
  if (gameShows && gameShown) {
    gameFlush();
  } else {
    if (gameShown) flushForced = true;   // the run sent only part of each frame
    flushChanged();
  }
  gameShown = gameShows;

  if (millis()-lastSerialMs > 1200) {
    lastSerialMs = millis();
    Serial.print("app="); Serial.print(currentApp);
    Serial.print(" mood="); Serial.print(faceMood);
    Serial.print(" wifi="); Serial.print(wifiManager.isConnected() ? "up" : "down");
    Serial.print(" ip="); Serial.print(WiFi.localIP());   // buddy_link.py learns the address here
    Serial.print(" timer="); Serial.print(timerStartMs != 0);   // and follows the timer when Wi-Fi is down
    Serial.print(" fps="); Serial.print(statusFrames * 1000UL / (millis() - statusFramesAt + 1));
    Serial.printf(" knock=%.2f", knockBiggest);   // the largest jolt since the last line, to tune KNOCK_G
    knockBiggest = 0;
    statusFrames = 0;
    statusFramesAt = millis();
    // Uptime and why the chip last started, so a restart nobody watched still
    // shows up the next time a serial monitor is opened.
    Serial.printf(" up=%lus rst=%d\n", (unsigned long)(millis() / 1000), (int)esp_reset_reason());
  }
  statusFrames++;
  // Frames come every FRAME_MS at most; a run of the game gets every frame the screen can take.
  static uint32_t frameAt = 0;
  const uint32_t spent = millis() - frameAt;
  if (currentApp != PAGE_GAME || !gameRunning()) delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
  frameAt = millis();
}