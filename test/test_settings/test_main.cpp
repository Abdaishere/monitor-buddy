#include <unity.h>
#include <string.h>
#include <string>
#include <ArduinoJson.h>
#include "settings.h"
#include "persona.h"
#include "protocol.h"

void setUp() {}
void tearDown() {}

static Settings makeDefaults() {
  Settings s{};
  s.mount = MOUNT_LEFT;
  s.theme = 2;
  s.brightness = 180;
  strcpy(s.githubUser, "IdefixRC");
  strcpy(s.tzName, "Asia/Singapore");
  strcpy(s.tzRule, "<+08>-8");
  strcpy(s.ticker, "AAPL");
  strcpy(s.stocks[0].symbol, "AAPL"); s.stocks[0].on = true; s.stockCount = 1;
  strcpy(s.stockKey, "");
  strcpy(s.coins[0].pair, "BTC-USD"); s.coins[0].source = COIN_COINBASE; s.coins[0].on = true; s.coinCount = 1;
  strcpy(s.lat, "1.3119");
  strcpy(s.lon, "103.8149");
  s.clockFormat = CLOCK_24H;
  s.tempUnit = TEMP_CELSIUS;
  s.windUnit = WIND_KMH;
  s.night = {false, 23, 7, 26};
  for (uint8_t i = 0; i < SETTINGS_PAGE_COUNT; i++) s.pages[i] = true;
  for (uint8_t i = 0; i < SETTINGS_MORE_COUNT; i++) s.morePages[i] = true;
  s.work = {9, 17, 12};
  return s;
}

static std::string dump(const Settings &s) {
  JsonDocument d;
  settingsToJson(s, d.to<JsonObject>());
  std::string out;
  serializeJson(d, out);
  return out;
}

static bool hasAdded(const AddedKeys &a, const char *key) {
  for (uint8_t i = 0; i < a.count; i++)
    if (strcmp(a.keys[i], key) == 0) return true;
  return false;
}

// Defaults as a stored file, ready to be edited by a test.
static void defaultsAsFile(JsonDocument &file) {
  settingsToJson(makeDefaults(), file.to<JsonObject>());
}

// ── settingsToJson ────────────────────────────────────────

static void test_to_json_writes_every_key() {
  JsonDocument d;
  settingsToJson(makeDefaults(), d.to<JsonObject>());
  TEST_ASSERT_EQUAL(CONFIG_VERSION, d["v"].as<int>());
  TEST_ASSERT_EQUAL_STRING("left", d["mount"]);
  TEST_ASSERT_EQUAL(2, d["theme"].as<int>());
  TEST_ASSERT_EQUAL(180, d["brightness"].as<int>());
  TEST_ASSERT_EQUAL_STRING("IdefixRC", d["github_user"]);
  TEST_ASSERT_EQUAL_STRING("Asia/Singapore", d["tz_name"]);
  TEST_ASSERT_EQUAL_STRING("<+08>-8", d["tz_rule"]);
  TEST_ASSERT_EQUAL_STRING("AAPL", d["ticker"]);
  TEST_ASSERT_EQUAL_STRING("AAPL", d["stocks"][0]["symbol"]);
  TEST_ASSERT_TRUE(d["stocks"][0]["on"].as<bool>());
  TEST_ASSERT_EQUAL(1, d["stocks"].size());
  TEST_ASSERT_EQUAL_STRING("", d["stock_key"]);
  TEST_ASSERT_EQUAL_STRING("1.3119", d["lat"]);
  TEST_ASSERT_EQUAL_STRING("103.8149", d["lon"]);
  TEST_ASSERT_EQUAL_STRING("celsius", d["temp_unit"]);
  TEST_ASSERT_EQUAL_STRING("kmh", d["wind_unit"]);
  for (uint8_t i = 0; i < SETTINGS_PAGE_COUNT; i++)
    TEST_ASSERT_TRUE(d["pages"][SETTINGS_PAGE_KEYS[i]].as<bool>());
}

// ── settingsLoad ──────────────────────────────────────────

static void test_load_without_file_keeps_defaults_and_adds_nothing() {
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), JsonVariantConst(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING(dump(makeDefaults()).c_str(), dump(out).c_str());
  TEST_ASSERT_EQUAL(0, added.count);
  TEST_ASSERT_EQUAL(0, inv.size());
}

static void test_load_applies_every_key() {
  JsonDocument file;
  deserializeJson(file,
    "{\"v\":1,\"mount\":\"right\",\"theme\":4,\"brightness\":99,\"github_user\":\"octo-cat\","
    "\"tz_name\":\"Europe/Berlin\",\"tz_rule\":\"CET-1CEST,M3.5.0,M10.5.0/3\","
    "\"ticker\":\"AVGO\",\"stocks\":[{\"symbol\":\"AVGO\",\"on\":true}],\"stock_key\":\"abc123\",\"coins\":[{\"pair\":\"BTC-USD\",\"source\":\"coinbase\",\"on\":true}],\"lat\":\"48.1374\",\"lon\":\"11.5755\","
    "\"clock_format\":\"12h\",\"temp_unit\":\"fahrenheit\",\"wind_unit\":\"mph\","
    "\"night\":{\"enabled\":true,\"start\":22,\"end\":6,\"brightness\":40},"
    "\"pages\":{\"face\":true,\"clock\":false,\"date\":true,\"weather\":true,"
    "\"moon\":false,\"stock\":true,\"github\":true,\"coin\":true},\"name\":\"Sam K.\",\"buddy_name\":\"Pixel\",\"greeting\":\"salam\",\"greeting_text\":\"Ahoy, captain!\",\"work\":{\"start\":8,\"end\":16,\"lunch\":13},\"weekend\":\"fri-sat\",\"birthday\":\"03-14\",\"since\":\"2026-11-20\",\"break_min\":50,\"water\":true,\"message\":\"You've got this\",\"face\":\"robot\","
    "\"more_pages\":{\"abda\":true,\"odoo\":true,\"prs\":false,\"gifs\":true,\"today\":true,\"game\":false,\"claude\":true},"
    "\"page_order\":[\"date\",\"face\"]}");
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL(MOUNT_RIGHT, out.mount);
  TEST_ASSERT_EQUAL_STRING("Sam K.", out.name);
  TEST_ASSERT_EQUAL_STRING("Pixel", out.buddyName);
  TEST_ASSERT_EQUAL(3, out.greeting);
  TEST_ASSERT_EQUAL_STRING("Ahoy, captain!", out.greetingText);
  TEST_ASSERT_EQUAL(8, out.work.start);
  TEST_ASSERT_EQUAL(16, out.work.end);
  TEST_ASSERT_EQUAL(13, out.work.lunch);
  TEST_ASSERT_EQUAL(1, out.weekend);
  TEST_ASSERT_EQUAL_STRING("03-14", out.birthday);
  TEST_ASSERT_EQUAL_STRING("2026-11-20", out.since);
  TEST_ASSERT_EQUAL(50, out.breakMin);
  TEST_ASSERT_TRUE(out.water);
  TEST_ASSERT_EQUAL_STRING("You've got this", out.message);
  TEST_ASSERT_EQUAL(FACE_ROBOT, out.face);
  TEST_ASSERT_FALSE(out.morePages[2]);   // prs
  TEST_ASSERT_TRUE(out.morePages[1]);
  TEST_ASSERT_EQUAL(2, out.pageOrderCount);
  TEST_ASSERT_EQUAL(2, out.pageOrder[0]);   // date
  TEST_ASSERT_EQUAL(4, out.theme);
  TEST_ASSERT_EQUAL_STRING("octo-cat", out.githubUser);
  TEST_ASSERT_EQUAL_STRING("Europe/Berlin", out.tzName);
  TEST_ASSERT_EQUAL_STRING("CET-1CEST,M3.5.0,M10.5.0/3", out.tzRule);
  TEST_ASSERT_EQUAL_STRING("AVGO", out.ticker);
  TEST_ASSERT_EQUAL_STRING("abc123", out.stockKey);
  TEST_ASSERT_EQUAL_STRING("48.1374", out.lat);
  TEST_ASSERT_EQUAL_STRING("11.5755", out.lon);
  TEST_ASSERT_EQUAL(TEMP_FAHRENHEIT, out.tempUnit);
  TEST_ASSERT_EQUAL(WIND_MPH, out.windUnit);
  TEST_ASSERT_EQUAL(CLOCK_12H_FORMAT, out.clockFormat);
  TEST_ASSERT_TRUE(out.night.enabled);
  TEST_ASSERT_EQUAL(22, out.night.start);
  TEST_ASSERT_EQUAL(6, out.night.end);
  TEST_ASSERT_EQUAL(40, out.night.brightness);
  TEST_ASSERT_FALSE(out.pages[1]);
  TEST_ASSERT_FALSE(out.pages[4]);
  TEST_ASSERT_TRUE(out.pages[5]);
  TEST_ASSERT_EQUAL(0, added.count);
  TEST_ASSERT_EQUAL(0, inv.size());
}

static void test_load_missing_key_keeps_default_and_reports_it() {
  JsonDocument file;
  defaultsAsFile(file);
  file.remove("ticker");
  file["theme"] = 4;
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL(4, out.theme);
  TEST_ASSERT_EQUAL_STRING("AAPL", out.ticker);
  TEST_ASSERT_EQUAL(1, added.count);
  TEST_ASSERT_TRUE(hasAdded(added, "ticker"));
}

static void test_load_missing_page_is_reported() {
  JsonDocument file;
  defaultsAsFile(file);
  file["pages"].remove("github");
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_TRUE(out.pages[6]);
  TEST_ASSERT_TRUE(hasAdded(added, "pages.github"));
}

static void test_load_invalid_value_keeps_default() {
  JsonDocument file;
  defaultsAsFile(file);
  file["theme"] = 9;
  file["lat"] = "north";
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL(2, out.theme);
  TEST_ASSERT_EQUAL_STRING("1.3119", out.lat);
  TEST_ASSERT_FALSE(inv["theme"].isNull());
  TEST_ASSERT_FALSE(inv["lat"].isNull());
  TEST_ASSERT_EQUAL(0, added.count);
}

static void test_load_ignores_unknown_keys() {
  JsonDocument file;
  defaultsAsFile(file);
  file["future_key"] = 1;
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING(dump(makeDefaults()).c_str(), dump(out).c_str());
  TEST_ASSERT_EQUAL(0, added.count);
  TEST_ASSERT_EQUAL(0, inv.size());
}

// ── settingsMerge ─────────────────────────────────────────

static void test_merge_partial_changes_only_that_key() {
  JsonDocument patch;
  deserializeJson(patch, "{\"ticker\":\"AVGO\"}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_TRUE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  Settings expected = makeDefaults();
  strcpy(expected.ticker, "AVGO");
  strcpy(expected.stocks[0].symbol, "AVGO");
  TEST_ASSERT_EQUAL_STRING(dump(expected).c_str(), dump(out).c_str());
}

static void test_merge_partial_pages() {
  JsonDocument patch;
  deserializeJson(patch, "{\"pages\":{\"stock\":false}}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_TRUE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_FALSE(out.pages[5]);
  TEST_ASSERT_TRUE(out.pages[0]);
  TEST_ASSERT_TRUE(out.pages[6]);
}

static void test_merge_rejects_bad_values_with_errors() {
  JsonDocument patch;
  deserializeJson(patch,
    "{\"theme\":9,\"lat\":\"95\",\"mount\":\"up\",\"ticker\":\"aapl\","
    "\"temp_unit\":\"kelvin\",\"pages\":{\"clock\":\"yes\"}}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_FALSE(errs["theme"].isNull());
  TEST_ASSERT_FALSE(errs["lat"].isNull());
  TEST_ASSERT_FALSE(errs["mount"].isNull());
  TEST_ASSERT_FALSE(errs["ticker"].isNull());
  TEST_ASSERT_FALSE(errs["temp_unit"].isNull());
  TEST_ASSERT_FALSE(errs["pages.clock"].isNull());
}

static void test_merge_rejects_non_object() {
  JsonDocument patch;
  deserializeJson(patch, "[1]");
  Settings out; JsonDocument errs;
  TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_FALSE(errs["config"].isNull());
}

static void test_merge_accepts_empty_github_user_and_ignores_unknown() {
  JsonDocument patch;
  deserializeJson(patch, "{\"github_user\":\"\",\"v\":1,\"future_key\":true}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_TRUE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_EQUAL_STRING("", out.githubUser);
  TEST_ASSERT_EQUAL(0, errs.size());
}

static void test_merge_rejects_non_decimal_coordinates() {
  const char *bad[] = {" 48", "\n48", "0x1A", "1e1"};
  for (const char *v : bad) {
    JsonDocument patch;
    patch.to<JsonObject>()["lat"] = v;
    Settings out; JsonDocument errs;
    TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
    TEST_ASSERT_FALSE(errs["lat"].isNull());
  }
}

static void test_merge_rejects_nan_inf_and_out_of_range_coordinates() {
  const char *badLat[] = {"nan", "inf", "91"};
  for (const char *v : badLat) {
    JsonDocument patch;
    patch.to<JsonObject>()["lat"] = v;
    Settings out; JsonDocument errs;
    TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
    TEST_ASSERT_FALSE(errs["lat"].isNull());
  }
  JsonDocument patch;
  patch.to<JsonObject>()["lon"] = "-181";
  Settings out; JsonDocument errs;
  TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_FALSE(errs["lon"].isNull());
}

static void test_merge_accepts_etc_gmt_tz_name() {
  JsonDocument patch;
  deserializeJson(patch, "{\"tz_name\":\"Etc/GMT+8\"}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_TRUE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_EQUAL_STRING("Etc/GMT+8", out.tzName);
}

static void test_merge_rejects_url_breaking_characters() {
  JsonDocument patch;
  deserializeJson(patch, "{\"stock_key\":\"abc&symbol=X\",\"github_user\":\"a/b\"}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_FALSE(errs["stock_key"].isNull());
  TEST_ASSERT_FALSE(errs["github_user"].isNull());
}

static void test_personal_settings_are_checked() {
  JsonDocument patch;
  deserializeJson(patch, "{\"work\":{\"start\":18,\"end\":9},\"greeting\":\"howdy\",\"weekend\":\"sun\","
                         "\"birthday\":\"3 March\",\"break_min\":500,\"water\":1,\"message\":\"a\\\"b\","
                         "\"buddy_name\":\"averyveryverylongname\",\"face\":\"dog\"}");
  Settings out; JsonDocument errs;
  TEST_ASSERT_FALSE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  for (const char *key : {"work", "greeting", "weekend", "birthday", "break_min", "water", "message", "buddy_name", "face"})
    TEST_ASSERT_FALSE_MESSAGE(errs[key].isNull(), key);
}

static void test_a_file_from_before_the_personal_settings_keeps_working() {
  JsonDocument file;
  defaultsAsFile(file);
  for (const char *key : {"name", "buddy_name", "greeting", "greeting_text", "work", "weekend", "birthday", "since", "break_min", "water", "message", "face"})
    file.remove(key);
  file["theme"] = 4;
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL(4, out.theme);                 // what the file had is kept
  TEST_ASSERT_EQUAL(0, inv.size());                // nothing in it counts as wrong
  TEST_ASSERT_EQUAL(12, added.count);              // the new keys are reported and take their defaults
  TEST_ASSERT_EQUAL(9, out.work.start);
  TEST_ASSERT_EQUAL(17, out.work.end);
  TEST_ASSERT_FALSE(out.water);
  TEST_ASSERT_EQUAL(0, out.breakMin);
  TEST_ASSERT_EQUAL_STRING("", out.buddyName);
}

// ── Round trip ────────────────────────────────────────────

static void test_round_trip_through_json() {
  Settings s = makeDefaults();
  s.mount = MOUNT_RIGHT; s.theme = 0; s.windUnit = WIND_MPH; s.pages[3] = false;
  strcpy(s.tzRule, "EST5EDT,M3.2.0,M11.1.0");
  JsonDocument file;
  deserializeJson(file, dump(s));
  Settings out; AddedKeys added; JsonDocument inv;
  settingsLoad(makeDefaults(), file.as<JsonVariantConst>(), out, added, inv.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING(dump(s).c_str(), dump(out).c_str());
  TEST_ASSERT_EQUAL(0, added.count);
  TEST_ASSERT_EQUAL(0, inv.size());
}

// ── wifiValidate ──────────────────────────────────────────

static bool runWifi(const char *json, char *ssid, char *pass, JsonDocument &errs) {
  JsonDocument w;
  deserializeJson(w, json);
  return wifiValidate(w.as<JsonVariantConst>(), ssid, 33, pass, 64, errs.to<JsonObject>());
}

static void test_wifi_valid() {
  char ssid[33], pass[64]; JsonDocument errs;
  TEST_ASSERT_TRUE(runWifi("{\"ssid\":\"Home\",\"password\":\"secret123\"}", ssid, pass, errs));
  TEST_ASSERT_EQUAL_STRING("Home", ssid);
  TEST_ASSERT_EQUAL_STRING("secret123", pass);
}

static void test_wifi_open_network() {
  char ssid[33], pass[64]; JsonDocument errs;
  TEST_ASSERT_TRUE(runWifi("{\"ssid\":\"Cafe\",\"password\":\"\"}", ssid, pass, errs));
  TEST_ASSERT_EQUAL_STRING("", pass);
}

static void test_wifi_password_is_required() {
  char ssid[33], pass[64]; JsonDocument errs;
  TEST_ASSERT_FALSE(runWifi("{\"ssid\":\"Cafe\"}", ssid, pass, errs));
  TEST_ASSERT_EQUAL_STRING("must be text (empty for an open network)", errs["wifi.password"] | "");
  TEST_ASSERT_TRUE(errs["wifi.ssid"].isNull());
  TEST_ASSERT_FALSE(runWifi("{\"ssid\":\"Cafe\",\"password\":null}", ssid, pass, errs));
  TEST_ASSERT_EQUAL_STRING("must be text (empty for an open network)", errs["wifi.password"] | "");
  TEST_ASSERT_FALSE(runWifi("{\"ssid\":\"Cafe\",\"password\":12345678}", ssid, pass, errs));
  TEST_ASSERT_FALSE(errs["wifi.password"].isNull());
}

static void test_wifi_rejects_bad_input() {
  char ssid[33], pass[64]; JsonDocument errs;
  TEST_ASSERT_FALSE(runWifi("{\"ssid\":\"\",\"password\":\"secret123\"}", ssid, pass, errs));
  TEST_ASSERT_FALSE(errs["wifi.ssid"].isNull());
  TEST_ASSERT_FALSE(runWifi("{\"ssid\":\"Home\",\"password\":\"short77\"}", ssid, pass, errs));
  TEST_ASSERT_FALSE(errs["wifi.password"].isNull());
  TEST_ASSERT_FALSE(runWifi("{\"ssid\":\"123456789012345678901234567890123\",\"password\":\"\"}", ssid, pass, errs));
  TEST_ASSERT_FALSE(errs["wifi.ssid"].isNull());
  TEST_ASSERT_FALSE(runWifi("\"Home\"", ssid, pass, errs));
  TEST_ASSERT_FALSE(errs["wifi"].isNull());
}

// ── tzRuleFromOffset ──────────────────────────────────────

static void test_tz_rule_whole_hours_east() {
  char r[24];
  TEST_ASSERT_TRUE(tzRuleFromOffset(8, r, sizeof r));
  TEST_ASSERT_EQUAL_STRING("<+08>-8", r);
}

static void test_tz_rule_whole_hours_west() {
  char r[24];
  TEST_ASSERT_TRUE(tzRuleFromOffset(-5, r, sizeof r));
  TEST_ASSERT_EQUAL_STRING("<-05>5", r);
}

static void test_tz_rule_half_and_quarter_hours() {
  char r[24];
  TEST_ASSERT_TRUE(tzRuleFromOffset(5.5f, r, sizeof r));
  TEST_ASSERT_EQUAL_STRING("<+0530>-5:30", r);
  TEST_ASSERT_TRUE(tzRuleFromOffset(5.75f, r, sizeof r));
  TEST_ASSERT_EQUAL_STRING("<+0545>-5:45", r);
  TEST_ASSERT_TRUE(tzRuleFromOffset(-3.5f, r, sizeof r));
  TEST_ASSERT_EQUAL_STRING("<-0330>3:30", r);
}

static void test_tz_rule_utc() {
  char r[24];
  TEST_ASSERT_TRUE(tzRuleFromOffset(0, r, sizeof r));
  TEST_ASSERT_EQUAL_STRING("UTC0", r);
}

static void test_tz_rule_rejects_out_of_range_and_small_buffer() {
  char r[24];
  TEST_ASSERT_FALSE(tzRuleFromOffset(15, r, sizeof r));
  TEST_ASSERT_FALSE(tzRuleFromOffset(-13, r, sizeof r));
  char tiny[4];
  TEST_ASSERT_FALSE(tzRuleFromOffset(8, tiny, sizeof tiny));
}

// The firmware copies its level into `current` first, so a SET without the key
// leaves it alone (src/main.cpp, mbSet).
static void test_merge_keeps_brightness_when_the_key_is_absent() {
  Settings current = makeDefaults(), out{};
  current.brightness = 120;
  JsonDocument patch, errs;
  deserializeJson(patch, "{\"theme\":1}");
  TEST_ASSERT_TRUE(settingsMerge(current, patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_EQUAL(120, out.brightness);
  TEST_ASSERT_EQUAL(1, out.theme);
}

static void test_merge_takes_a_new_brightness() {
  Settings out{};
  JsonDocument patch, errs;
  deserializeJson(patch, "{\"brightness\":200}");
  TEST_ASSERT_TRUE(settingsMerge(makeDefaults(), patch.as<JsonVariantConst>(), out, errs.to<JsonObject>()));
  TEST_ASSERT_EQUAL(200, out.brightness);
}

// ── The @MB line budget (spec §4.6) ───────────────────────

// Every personal setting at its longest.
static void maxPersonal(Settings &s) {
  memset(s.name, 'W', 12);          s.name[12] = '\0';
  memset(s.buddyName, 'W', 12);     s.buddyName[12] = '\0';
  s.greeting = GREETING_CUSTOM;
  memset(s.greetingText, 'W', 26);  s.greetingText[26] = '\0';
  s.work = {10, 24, 13};
  s.weekend = WEEKEND_FRI_SAT;
  strcpy(s.birthday, "12-31");
  strcpy(s.since, "2026-11-20");
  s.breakMin = 120;
  s.water = false;
  memset(s.message, 'W', 26);       s.message[26] = '\0';
  s.face = FACE_MINIMAL;
}

// The largest GET reply a board can really send: every text field at its
// maximum, five stocks and five coins. A key missing from a file holds its
// short default, so "every field at max" and "every path added" cannot happen
// together; after a 1.2 -> 1.3 update only the new keys are reported, which
// are "brightness", "clock_format", "night", "coins" and "pages.coin" (the paths #24 added
// are in the line, with no reserve). The line must fit the 2,048-byte reply
// buffer with a fifth to spare: 1,580 bytes with every personal setting at
// its longest too.
static void test_largest_get_reply_fits_with_room() {
  Settings s = makeDefaults();
  s.mount = MOUNT_RIGHT; s.theme = 4;
  memset(s.githubUser, 'a', 39); s.githubUser[39] = '\0';
  memset(s.tzName, 'a', 64);     s.tzName[64] = '\0';
  memset(s.tzRule, 'A', 64);     s.tzRule[64] = '\0';
  memset(s.ticker, 'A', 10);     s.ticker[10] = '\0';
  for (uint8_t i = 0; i < SETTINGS_STOCK_MAX; i++) { memset(s.stocks[i].symbol, 'A', 10); s.stocks[i].symbol[10] = '\0'; s.stocks[i].on = true; }
  s.stockCount = SETTINGS_STOCK_MAX;
  memset(s.stockKey, 'k', 64);   s.stockKey[64] = '\0';
  for (uint8_t i = 0; i < SETTINGS_COIN_MAX; i++) {
    memset(s.coins[i].pair, 'A', 10); s.coins[i].pair[10] = '-'; memset(s.coins[i].pair + 11, 'B', 10); s.coins[i].pair[21] = '\0';
    s.coins[i].source = COIN_COINBASE; s.coins[i].on = true;   // the longer word
  }
  s.coinCount = SETTINGS_COIN_MAX;
  strcpy(s.lat, "-12.34567890123");
  strcpy(s.lon, "-123.4567890123");
  s.tempUnit = TEMP_FAHRENHEIT;
  s.clockFormat = CLOCK_12H_FORMAT;
  s.brightness = 255;
  s.night = {false, 23, 22, 255};
  maxPersonal(s);
  for (uint8_t i = 0; i < SETTINGS_ROUND_COUNT; i++) s.pageOrder[i] = i;   // every page named
  s.pageOrderCount = SETTINGS_ROUND_COUNT;
  JsonDocument d;
  d["ok"] = true; d["cmd"] = "GET"; d["id"] = 4294967295UL;
  d["v"] = CONFIG_VERSION; d["stored"] = true;
  settingsToJson(s, d["config"].to<JsonObject>());
  JsonArray added = d["added"].to<JsonArray>();
  const char *all[] = {"brightness", "clock_format", "night", "coins", "pages.coin"};
  for (const char *k : all) added.add(k);
  size_t line = 4 + measureJson(d) + 1;   // "@MB " + JSON + "\n"
  TEST_ASSERT_LESS_OR_EQUAL(LineReader::CAPACITY * 4 / 5, line);   // a fifth of the line to spare
}

// The largest SET line the flasher sends: the whole config plus Wi-Fi.
static void test_largest_set_line_fits_with_room() {
  Settings s = makeDefaults();
  s.mount = MOUNT_RIGHT; s.theme = 4;
  memset(s.githubUser, 'a', 39); s.githubUser[39] = '\0';
  memset(s.tzName, 'a', 64);     s.tzName[64] = '\0';
  memset(s.tzRule, 'A', 64);     s.tzRule[64] = '\0';
  for (uint8_t i = 0; i < SETTINGS_STOCK_MAX; i++) { memset(s.stocks[i].symbol, 'A', 10); s.stocks[i].symbol[10] = '\0'; s.stocks[i].on = true; }
  s.stockCount = SETTINGS_STOCK_MAX;
  memset(s.ticker, 'A', 10);     s.ticker[10] = ' ';
  memset(s.stockKey, 'k', 64);   s.stockKey[64] = '\0';
  for (uint8_t i = 0; i < SETTINGS_COIN_MAX; i++) {
    memset(s.coins[i].pair, 'A', 10); s.coins[i].pair[10] = '-'; memset(s.coins[i].pair + 11, 'B', 10); s.coins[i].pair[21] = '\0';
    s.coins[i].source = COIN_COINBASE; s.coins[i].on = true;   // the longer word
  }
  s.coinCount = SETTINGS_COIN_MAX;
  strcpy(s.lat, "-12.34567890123");
  strcpy(s.lon, "-123.4567890123");
  s.tempUnit = TEMP_FAHRENHEIT;
  s.clockFormat = CLOCK_12H_FORMAT;
  s.brightness = 255;
  s.night = {false, 23, 22, 255};
  maxPersonal(s);
  for (uint8_t i = 0; i < SETTINGS_ROUND_COUNT; i++) s.pageOrder[i] = i;   // every page named
  s.pageOrderCount = SETTINGS_ROUND_COUNT;
  JsonDocument d;
  d["id"] = 4294967295UL;
  settingsToJson(s, d["config"].to<JsonObject>());
  d["wifi"]["ssid"] = std::string(32, 's');
  d["wifi"]["password"] = std::string(63, 'p');
  size_t line = 8 + measureJson(d) + 1;   // "@MB SET " + JSON + "\n"
  TEST_ASSERT_LESS_OR_EQUAL(LineReader::CAPACITY * 4 / 5, line);   // a fifth of the line to spare
}

// ── The round of pages: "page_order" and "more_pages" ──────
static std::string roundOf(const Settings &s) {
  uint8_t round[SETTINGS_ROUND_COUNT];
  settingsRound(s, round);
  std::string out;
  for (uint8_t i = 0; i < SETTINGS_ROUND_COUNT; i++) { if (i) out += ' '; out += SETTINGS_ROUND_KEYS[round[i]]; }
  return out;
}

void test_the_round_without_an_order_is_the_usual_one() {
  Settings s = makeDefaults();
  TEST_ASSERT_EQUAL_STRING("face clock date weather moon stock coin github odoo today claude prs gifs game abda", roundOf(s).c_str());
  JsonDocument d;
  settingsToJson(s, d.to<JsonObject>());
  TEST_ASSERT_EQUAL(0, d["page_order"].as<JsonArray>().size());
  TEST_ASSERT_TRUE(d["more_pages"]["odoo"].as<bool>());
  TEST_ASSERT_TRUE(d["more_pages"]["game"].as<bool>());
}

void test_page_order_puts_its_pages_first_and_keeps_the_rest() {
  Settings s = makeDefaults(), out;
  JsonDocument patch, errors;
  deserializeJson(patch, "{\"page_order\":[\"clock\",\"today\",\"face\",\"clock\"],\"more_pages\":{\"game\":false}}");
  TEST_ASSERT_TRUE(settingsMerge(s, patch, out, errors.to<JsonObject>()));
  TEST_ASSERT_EQUAL(4, out.pageOrderCount);
  // a page named twice counts once; the pages left out follow in the usual order
  TEST_ASSERT_EQUAL_STRING("clock today face date weather moon stock coin github odoo claude prs gifs game abda", roundOf(out).c_str());
  TEST_ASSERT_FALSE(out.morePages[13 - SETTINGS_PAGE_COUNT]);   // game
  TEST_ASSERT_TRUE(out.morePages[9 - SETTINGS_PAGE_COUNT]);     // odoo: a group merges per field
  // through JSON and back
  JsonDocument d;
  settingsToJson(out, d.to<JsonObject>());
  TEST_ASSERT_EQUAL_STRING("today", d["page_order"][1].as<const char *>());
  Settings again;
  AddedKeys added;
  JsonDocument invalid;
  settingsLoad(makeDefaults(), d, again, added, invalid.to<JsonObject>());
  TEST_ASSERT_EQUAL(0, invalid.size());
  TEST_ASSERT_EQUAL_STRING(roundOf(out).c_str(), roundOf(again).c_str());
  TEST_ASSERT_FALSE(again.morePages[13 - SETTINGS_PAGE_COUNT]);
}

void test_page_order_refuses_what_is_not_a_page() {
  Settings s = makeDefaults(), out;
  JsonDocument patch, errors;
  deserializeJson(patch, "{\"page_order\":[\"clock\",\"settings\"]}");
  TEST_ASSERT_FALSE(settingsMerge(s, patch, out, errors.to<JsonObject>()));
  TEST_ASSERT_FALSE(errors["page_order[1]"].isNull());
  patch.clear(); errors.clear();
  deserializeJson(patch, "{\"page_order\":\"clock\"}");
  TEST_ASSERT_FALSE(settingsMerge(s, patch, out, errors.to<JsonObject>()));
  TEST_ASSERT_FALSE(errors["page_order"].isNull());
  patch.clear(); errors.clear();
  deserializeJson(patch, "{\"more_pages\":{\"odoo\":\"no\"}}");
  TEST_ASSERT_FALSE(settingsMerge(s, patch, out, errors.to<JsonObject>()));
  TEST_ASSERT_FALSE(errors["more_pages.odoo"].isNull());
  // an empty list is the usual order again
  patch.clear(); errors.clear();
  s.pageOrder[0] = 1; s.pageOrderCount = 1;
  deserializeJson(patch, "{\"page_order\":[]}");
  TEST_ASSERT_TRUE(settingsMerge(s, patch, out, errors.to<JsonObject>()));
  TEST_ASSERT_EQUAL(0, out.pageOrderCount);
}

void test_a_file_from_before_the_page_order_keeps_working() {
  Settings out;
  AddedKeys added;
  JsonDocument stored, invalid;
  deserializeJson(stored, "{\"v\":2,\"theme\":3,\"pages\":{\"moon\":false}}");
  settingsLoad(makeDefaults(), stored, out, added, invalid.to<JsonObject>());
  TEST_ASSERT_EQUAL(0, invalid.size());
  TEST_ASSERT_EQUAL(0, out.pageOrderCount);
  for (uint8_t i = 0; i < SETTINGS_MORE_COUNT; i++) TEST_ASSERT_TRUE(out.morePages[i]);
  TEST_ASSERT_EQUAL_STRING("face clock date weather moon stock coin github odoo today claude prs gifs game abda", roundOf(out).c_str());
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_merge_keeps_brightness_when_the_key_is_absent);
  RUN_TEST(test_merge_takes_a_new_brightness);
  RUN_TEST(test_tz_rule_whole_hours_east);
  RUN_TEST(test_tz_rule_whole_hours_west);
  RUN_TEST(test_tz_rule_half_and_quarter_hours);
  RUN_TEST(test_tz_rule_utc);
  RUN_TEST(test_tz_rule_rejects_out_of_range_and_small_buffer);
  RUN_TEST(test_to_json_writes_every_key);
  RUN_TEST(test_load_without_file_keeps_defaults_and_adds_nothing);
  RUN_TEST(test_load_applies_every_key);
  RUN_TEST(test_load_missing_key_keeps_default_and_reports_it);
  RUN_TEST(test_load_missing_page_is_reported);
  RUN_TEST(test_load_invalid_value_keeps_default);
  RUN_TEST(test_load_ignores_unknown_keys);
  RUN_TEST(test_merge_partial_changes_only_that_key);
  RUN_TEST(test_merge_partial_pages);
  RUN_TEST(test_merge_rejects_bad_values_with_errors);
  RUN_TEST(test_merge_rejects_non_object);
  RUN_TEST(test_merge_accepts_empty_github_user_and_ignores_unknown);
  RUN_TEST(test_merge_rejects_non_decimal_coordinates);
  RUN_TEST(test_merge_rejects_nan_inf_and_out_of_range_coordinates);
  RUN_TEST(test_merge_accepts_etc_gmt_tz_name);
  RUN_TEST(test_merge_rejects_url_breaking_characters);
  RUN_TEST(test_personal_settings_are_checked);
  RUN_TEST(test_a_file_from_before_the_personal_settings_keeps_working);
  RUN_TEST(test_round_trip_through_json);
  RUN_TEST(test_the_round_without_an_order_is_the_usual_one);
  RUN_TEST(test_page_order_puts_its_pages_first_and_keeps_the_rest);
  RUN_TEST(test_page_order_refuses_what_is_not_a_page);
  RUN_TEST(test_a_file_from_before_the_page_order_keeps_working);
  RUN_TEST(test_wifi_valid);
  RUN_TEST(test_wifi_open_network);
  RUN_TEST(test_wifi_password_is_required);
  RUN_TEST(test_wifi_rejects_bad_input);
  RUN_TEST(test_largest_get_reply_fits_with_room);
  RUN_TEST(test_largest_set_line_fits_with_room);
  return UNITY_END();
}
