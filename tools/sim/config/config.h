// The simulated Buddy's config.h (tools/sim). The simulator build puts this
// folder ahead of config/ on the include path, so the real config/config.h and
// its secrets are never read.
//
// No keys here: like a released firmware, the simulated Buddy gets its link
// and update keys over USB ("@MB KEYS") and keeps them in /keys.dat. The test
// keys are in ../keys/.
#pragma once

#define MOUNTING_LOCATION LEFT
#define THEME 2
#define GITHUB_USER   "octocat"
#define TIMEZONE        "Asia/Dubai"
#define TZ_RULE         "<+04>-4"
#define TZ_OFFSET_HOURS 4
#define TICKER    "AAPL"
#define STOCKKEY  "simfinnhubkey"
#define LAT   "25.2048"
#define LONG  "55.2708"
#define TEMP  "celsius"
#define WIND  "kmh"

#define SHOW_FACE     true
#define SHOW_CLOCK    true
#define SHOW_DATE     true
#define SHOW_WEATHER  true
#define SHOW_MOON     true
#define SHOW_STOCK    true
#define SHOW_GITHUB   true

#define CLOCK_12H        false
#define NIGHT_MODE       true
#define NIGHT_START_HOUR 23
#define NIGHT_END_HOUR   7
#define NIGHT_BRIGHTNESS 26
#define NIGHT_WAKE_MS    30000UL

#define ODOO_TRIGRAM "SIM"
