#include "assets.h"
#include "palette.h"
#include "config.h"

#include <SPI.h>
#include <SPIFFS.h>
#include <SD.h>
#include <PNGdec.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {

TFT_eSPI *g_tft = nullptr;

// PNGdec's main decoder class is called `PNG`.
PNG g_png;

fs::FS *g_fs = nullptr;
File g_file;

int16_t g_drawX = 0;
int16_t g_drawY = 0;

// Line buffer for PNG decode.
uint16_t g_line[320];

bool g_spiffsReady = false;
bool g_sdReady = false;

SPIClass g_sdVspi(VSPI);
SPIClass g_sdHspi(HSPI);

constexpr uint32_t SD_SPI_HZ = 4000000;
constexpr uint32_t SD_SPI_HZ_FALLBACK = 1000000;
constexpr size_t FLAG_MAX_BYTES = 120 * 1024;

bool tryBeginSd(SPIClass &bus, const char *busName, uint32_t hz) {
  bus.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);
  const bool ok = SD.begin(SD_CS, bus, hz);
  Serial.printf("SD: %s @ %luHz -> %s\n",
                busName,
                (unsigned long)hz,
                ok ? "ready" : "fail");
  if (!ok) SD.end();
  return ok;
}

void *pngOpen(const char *filename, int32_t *size) {
  if (!g_fs) return nullptr;
  g_file = g_fs->open(filename, "r");
  if (!g_file) return nullptr;
  *size = (int32_t)g_file.size();
  return (void *)1;
}

void pngClose(void *handle) {
  (void)handle;
  if (g_file) g_file.close();
}

int32_t pngRead(PNGFILE *pFile, uint8_t *pBuf, int32_t len) {
  (void)pFile;
  if (!g_file) return 0;
  return (int32_t)g_file.read(pBuf, len);
}

int32_t pngSeek(PNGFILE *pFile, int32_t position) {
  (void)pFile;
  if (!g_file) return 0;
  return (int32_t)g_file.seek(position);
}

int pngDraw(PNGDRAW *pDraw) {
  if (!g_tft) return 0;
  // TFT_eSPI expects big-endian RGB565 pixel order when swap-bytes is disabled.
  g_png.getLineAsRGB565(pDraw, g_line, PNG_RGB565_BIG_ENDIAN, 0x00000000);
  const int16_t y = (int16_t)(g_drawY + pDraw->y);
  g_tft->pushImage(g_drawX, y, pDraw->iWidth, 1, g_line);
  return 1;
}

bool drawPngFromFs(fs::FS &fs, const String &path, int16_t x, int16_t y) {
  if (!g_tft) return false;

  g_fs = &fs;
  g_drawX = x;
  g_drawY = y;

  const int rcOpen = g_png.open((char *)path.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if (rcOpen != 0) return false;

  const int rcDec = g_png.decode(nullptr, 0);
  g_png.close();
  return (rcDec == 0);
}


String makeFlagSizePath(int16_t size, const String &abbr) {
  return String("/flags/") + String(size) + "/" + abbr + ".png";
}

String makeFlagFlatPath(const String &abbr) {
  return String("/flags/") + abbr + ".png";
}

bool ensureSpiffsDir(const String &dirPath) {
  if (!g_spiffsReady) return false;
  if (dirPath.length() == 0 || dirPath == "/") return true;

  int slash = 1;
  while (slash > 0) {
    slash = dirPath.indexOf('/', slash);
    String segment = (slash >= 0) ? dirPath.substring(0, slash) : dirPath;
    if (segment.length() > 1 && !SPIFFS.exists(segment)) {
      if (!SPIFFS.mkdir(segment) && !SPIFFS.exists(segment)) {
        return false;
      }
    }
    if (slash >= 0) slash++;
  }
  return true;
}

String rewriteEspnLogoUrlForSize(const String &url, int16_t size) {
  if (!url.length()) return url;

  String path = url;
  const int scheme = path.indexOf("://");
  if (scheme >= 0) {
    const int slash = path.indexOf('/', scheme + 3);
    if (slash >= 0) {
      path = path.substring(slash);
    }
  }

  const int query = path.indexOf('?');
  if (query >= 0) {
    path = path.substring(0, query);
  }

  return String("https://a.espncdn.com/combiner/i?img=") + path +
         "&w=" + String(size) + "&h=" + String(size);
}

bool downloadToSpiffs(const String &url, const String &destPath, size_t maxBytes) {
  if (!g_spiffsReady) return false;
  if (url.isEmpty()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;

  const int slash = destPath.lastIndexOf('/');
  if (slash > 0) {
    const String dir = destPath.substring(0, slash);
    if (!ensureSpiffsDir(dir)) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);

  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "olympic-scoreboard-esp32");
  http.addHeader("Accept", "image/png");

  const int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }

  const int len = http.getSize();
  if (len > 0 && (size_t)len > maxBytes) {
    http.end();
    return false;
  }

  File out = SPIFFS.open(destPath, "w");
  if (!out) {
    http.end();
    return false;
  }

  Stream &stream = http.getStream();
  uint8_t buf[1024];
  size_t total = 0;
  int remaining = len;

  while (http.connected() && (remaining > 0 || remaining == -1)) {
    size_t avail = (size_t)stream.available();
    if (!avail) {
      delay(1);
      continue;
    }

    const size_t want = (avail > sizeof(buf)) ? sizeof(buf) : avail;
    const int readN = stream.readBytes((char *)buf, want);
    if (readN <= 0) break;

    total += (size_t)readN;
    if (total > maxBytes) {
      out.close();
      SPIFFS.remove(destPath);
      http.end();
      return false;
    }

    if (out.write(buf, (size_t)readN) != (size_t)readN) {
      out.close();
      SPIFFS.remove(destPath);
      http.end();
      return false;
    }

    if (remaining > 0) remaining -= readN;
  }

  out.close();
  http.end();

  if (total == 0) {
    SPIFFS.remove(destPath);
    return false;
  }

  return true;
}

bool copySpiffsFile(const String &src, const String &dst) {
  if (!SPIFFS.exists(src)) return false;

  const int slash = dst.lastIndexOf('/');
  if (slash > 0) {
    const String dir = dst.substring(0, slash);
    if (!ensureSpiffsDir(dir)) return false;
  }

  File in = SPIFFS.open(src, "r");
  if (!in) return false;
  File out = SPIFFS.open(dst, "w");
  if (!out) {
    in.close();
    return false;
  }

  uint8_t buf[1024];
  while (in.available()) {
    const size_t n = in.read(buf, sizeof(buf));
    if (!n) break;
    if (out.write(buf, n) != n) {
      in.close();
      out.close();
      SPIFFS.remove(dst);
      return false;
    }
  }

  in.close();
  out.close();
  return true;
}

bool ensureFlagCached(const String &abbr, const String &logoUrl, int16_t size) {
  if (!g_spiffsReady) return false;

  const String sizedPath = makeFlagSizePath(size, abbr);
  const String flatPath = makeFlagFlatPath(abbr);
  if (SPIFFS.exists(sizedPath) || SPIFFS.exists(flatPath)) return true;

  if (!logoUrl.length()) return false;

  const String sizedUrl = rewriteEspnLogoUrlForSize(logoUrl, size);
  if (downloadToSpiffs(sizedUrl, sizedPath, FLAG_MAX_BYTES)) {
    if (!SPIFFS.exists(flatPath)) {
      copySpiffsFile(sizedPath, flatPath);
    }
    return true;
  }

  if (!SPIFFS.exists(flatPath) && downloadToSpiffs(logoUrl, flatPath, FLAG_MAX_BYTES)) {
    copySpiffsFile(flatPath, sizedPath);
    return true;
  }

  return SPIFFS.exists(sizedPath) || SPIFFS.exists(flatPath);
}

void drawFallbackBadge(int16_t x, int16_t y, int size, const char *label) {
  if (!g_tft) return;

  const int16_t radius = (int16_t)(size / 6);
  g_tft->fillRoundRect(x, y, size, size, radius, Palette::PANEL_2);
  g_tft->drawRoundRect(x, y, size, size, radius, Palette::FRAME);

  if (label && *label) {
    g_tft->setTextDatum(MC_DATUM);
    g_tft->setTextColor(Palette::WHITE, Palette::PANEL_2);
    g_tft->setTextFont(2);
    g_tft->drawString(label, (int16_t)(x + size / 2), (int16_t)(y + size / 2));
  }
}

void drawLogoImpl(TFT_eSPI &tft,
                  const String &abbr,
                  const String &logoUrl,
                  int16_t x,
                  int16_t y,
                  int16_t size) {
  if (!g_tft) g_tft = &tft;
  g_tft->fillRect(x, y, size, size, Palette::BG);

  if (!abbr.isEmpty()) {
    ensureFlagCached(abbr, logoUrl, size);
  }

  const String flagSized = makeFlagSizePath(size, abbr);
  const String flagFlat = makeFlagFlatPath(abbr);

  bool ok = false;
  if (g_spiffsReady) {
    if (!ok && SPIFFS.exists(flagSized)) ok = drawPngFromFs(SPIFFS, flagSized, x, y);
    if (!ok && SPIFFS.exists(flagFlat)) ok = drawPngFromFs(SPIFFS, flagFlat, x, y);
  }


  if (!ok) {
    drawFallbackBadge(x, y, size, abbr.c_str());
  }
}

} // namespace

namespace Assets {

void begin(TFT_eSPI &tft) {
  g_tft = &tft;
  g_tft->setSwapBytes(false);

  g_spiffsReady = SPIFFS.begin(true);
  Serial.println(g_spiffsReady ? "SPIFFS: ready" : "SPIFFS: FAIL");
  if (g_spiffsReady) {
    const size_t total = SPIFFS.totalBytes();
    const size_t used = SPIFFS.usedBytes();
    const size_t freeBytes = (total > used) ? (total - used) : 0;
    Serial.printf("SPIFFS: total=%u used=%u free=%u bytes\n",
                  (unsigned)total,
                  (unsigned)used,
                  (unsigned)freeBytes);
  }

#if ENABLE_SD_LOGOS
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  g_sdReady = tryBeginSd(g_sdVspi, "VSPI", SD_SPI_HZ);
  if (!g_sdReady) g_sdReady = tryBeginSd(g_sdHspi, "HSPI", SD_SPI_HZ);
  if (!g_sdReady) g_sdReady = tryBeginSd(g_sdVspi, "VSPI", SD_SPI_HZ_FALLBACK);
  if (!g_sdReady) g_sdReady = tryBeginSd(g_sdHspi, "HSPI", SD_SPI_HZ_FALLBACK);
  if (!g_sdReady) Serial.println("SD: not ready");
#else
  g_sdReady = false;
#endif

  if (g_spiffsReady) {
    ensureSpiffsDir("/flags");
    ensureSpiffsDir("/flags/56");
    ensureSpiffsDir("/flags/64");
    ensureSpiffsDir("/flags/96");
  }
}

bool drawPng(TFT_eSPI &tft, const String &path, int16_t x, int16_t y) {
  if (!g_tft) g_tft = &tft;

  if (g_spiffsReady && SPIFFS.exists(path)) {
    if (drawPngFromFs(SPIFFS, path, x, y)) return true;
  }

#if ENABLE_SD_LOGOS
  if (g_sdReady && SD.exists(path)) {
    if (drawPngFromFs(SD, path, x, y)) return true;
  }
#endif

  return false;
}

void drawLogo(TFT_eSPI &tft, const String &abbr, int16_t x, int16_t y, int16_t size) {
  drawLogoImpl(tft, abbr, String(""), x, y, size);
}

void drawLogo(TFT_eSPI &tft,
              const String &abbr,
              const String &logoUrl,
              int16_t x,
              int16_t y,
              int16_t size) {
  drawLogoImpl(tft, abbr, logoUrl, x, y, size);
}

bool sdReady() {
  return g_sdReady;
}

} // namespace Assets








