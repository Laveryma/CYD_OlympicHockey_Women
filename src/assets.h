#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

// Optional SD-logo support is configured via include/config.h.
// This header stays lightweight; implementation lives in assets.cpp.

namespace Assets {

// Call once after TFT is initialised.
void begin(TFT_eSPI &tft);

// Draw an image from SPIFFS/SD at x,y (top-left). Returns true on success.
bool drawPng(TFT_eSPI &tft, const String &path, int16_t x, int16_t y);

// Draw a team/country badge at x,y (top-left). Flag cache is preferred in SPIFFS.
void drawLogo(TFT_eSPI &tft, const String &abbr, int16_t x, int16_t y, int16_t size = 56);

// Draw with optional remote logo URL for first-use SPIFFS caching.
void drawLogo(TFT_eSPI &tft,
              const String &abbr,
              const String &logoUrl,
              int16_t x,
              int16_t y,
              int16_t size = 56);

// For diagnostics.
bool sdReady();

} // namespace Assets
