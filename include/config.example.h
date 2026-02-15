#pragma once

// ---- Wi-Fi ----
// Create your real `include/config.h` by copying this file and filling in the
// values below.
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Optional: RSSI hysteresis (dB) when both SSIDs are visible.
// Higher = stickier to the last network.
#define WIFI_RSSI_HYST_DB 6

// ---- Timezone (for local next-game time + countdown) ----
// This string is passed to `configTzTime(...)` on the ESP32.
// UK example:  "GMT0BST,M3.5.0/1,M10.5.0/2"
// ET example:  "EST5EDT,M3.2.0/2,M11.1.0/2"
#define TZ_INFO "GMT0BST,M3.5.0/1,M10.5.0/2"

// NTP servers used by the ESP32
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

// ---- Display rotation (TFT_eSPI setRotation) ----
// 0=portrait, 1=landscape, 2=portrait inverted, 3=landscape inverted
#define TFT_ROTATION 3

// ---- Team focus ----
// Canada women example: "CAN"
#define FOCUS_TEAM_ABBR "CAN"

// ---- Poll intervals (ms) ----
#define POLL_SCOREBOARD_MS   15000   // 15s
#define POLL_GAMEDETAIL_MS    8000   // 8s (only when a game is live)

// Optional SD access (disabled in esp32-cyd-sdfix).
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


// CYD backlight PWM channel
#define CYD_BL_PWM_CH 0

// Optional: define the touch controller CS pin to silence TFT_eSPI warnings.
// If you are not using touch in this firmware, this is purely cosmetic.
// Typical CYD (XPT2046) uses GPIO 33 for TOUCH_CS, but variants exist.
// #define TOUCH_CS 33

// DAC pin used for anthem playback (ESP32 DAC-capable pins: 25 or 26)
#ifndef ANTHEM_DAC_PIN
#define ANTHEM_DAC_PIN 25
#endif

// Optional secondary DAC pin for CYD wiring variants.
// Default is single-DAC mode (same pin as ANTHEM_DAC_PIN).
// Set to the other DAC pin (25/26) only if your board wiring requires mirroring.
#ifndef ANTHEM_DAC_PIN_ALT
#define ANTHEM_DAC_PIN_ALT ANTHEM_DAC_PIN
#endif

// Anthem output gain in percent. Increase if audio is too quiet.
// Keep at 100 unless your source audio is very quiet.
#ifndef ANTHEM_GAIN_PCT
#define ANTHEM_GAIN_PCT 100
#endif





