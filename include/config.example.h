#pragma once

/*
  CYD Olympic Hockey (Women) — Example configuration
  
  Edit include/config.h with your real values.

  IMPORTANT:
  - Do not commit include/config.h if it contains your Wi-Fi credentials.
  - WIFI_SSID_2 can be left as "" to disable fallback Wi-Fi.
*/

// -------------------- Wi-Fi (primary + fallback) --------------------
// Device prefers WIFI_SSID_1 when it is visible, otherwise it will try WIFI_SSID_2.
#define WIFI_SSID_1       ""
#define WIFI_PASSWORD_1   ""

#define WIFI_SSID_2       ""
#define WIFI_PASSWORD_2   ""

// Connection behaviour (matches include/config.h defaults)
#define WIFI_SCAN_BEFORE_CONNECT      1
#define WIFI_CONNECT_TIMEOUT_MS       15000
#define WIFI_RECONNECT_INTERVAL_MS    30000

// If connected to fallback, optionally roam back to primary when it returns.
#define WIFI_ROAM_TO_PRIMARY          0
#define WIFI_ROAM_CHECK_INTERVAL_MS   120000


// -------------------- Display --------------------
// TFT_eSPI setRotation:
// 0=portrait, 1=landscape, 2=portrait (inverted), 3=landscape (inverted)
#define TFT_ROTATION 1


// -------------------- Team focus --------------------
// 3-letter team abbreviation used by this project (and your flags).
#define FOCUS_TEAM_ABBR "CAN"


// -------------------- Poll intervals (ms) --------------------
#define POLL_SCOREBOARD_MS   15000   // 15s
#define POLL_GAMEDETAIL_MS   8000    // 8s (only when a game is live)


// -------------------- Optional SD access --------------------
// (disabled in esp32-cyd-sdfix)
#ifndef ENABLE_SD_LOGOS
  #define ENABLE_SD_LOGOS 1
#endif

// CYD ESP32-2432S028 microSD pins (usually VSPI wiring)
#ifndef SD_CS
  #define SD_CS   5
#endif
#ifndef SD_SCLK
  #define SD_SCLK 18
#endif
#ifndef SD_MISO
  #define SD_MISO 19
#endif
#ifndef SD_MOSI
  #define SD_MOSI 23
#endif


// -------------------- Backlight / input --------------------
#define CYD_BL_PWM_CH 0
#define BOOT_BTN_PIN  0


// -------------------- Time + countdown --------------------
// POSIX TZ string for Europe/London (DST aware). Change if you want a different local timezone.
// UK example:  "GMT0BST,M3.5.0/1,M10.5.0/2"
// ET example:  "EST5EDT,M3.2.0/2,M11.1.0/2"
#define TZ_INFO "GMT0BST,M3.5.0/1,M10.5.0/2"

#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"


// -------------------- Audio (anthem playback) --------------------
// ESP32 DAC-capable pins are 25 and 26.
#ifndef ANTHEM_DAC_PIN
  #define ANTHEM_DAC_PIN 25
#endif

// Optional Secondary DAC pin for CYD wiring variants.
// Default is single-DAC mode (same pin as ANTHEM_DAC_PIN).
// Set to the other DAC pin (25/26) only if your board wiring requires mirroring.
#ifndef ANTHEM_DAC_PIN_ALT
  #define ANTHEM_DAC_PIN_ALT 26
#endif

// Output gain in percent.
// Keep at 100 unless your source audio is very quiet.
#ifndef ANTHEM_GAIN_PCT
  #define ANTHEM_GAIN_PCT 220
#endif
