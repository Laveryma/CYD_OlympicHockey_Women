#include "ui.h"
#include "palette.h"
#include "assets.h"
#include "config.h"

#include <time.h>

static inline void drawCentered(TFT_eSPI &tft,
                                const String &s,
                                int x,
                                int y,
                                int font,
                                uint16_t fg,
                                uint16_t bg) {
  tft.setTextFont(font);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.drawString(s, x, y);
}

static inline void clearScreenWithRotation(TFT_eSPI &tft, uint8_t rotation) {
  // Avoid viewport clipping issues on some CYD panels; just rotate + clear.
  tft.setRotation(rotation);
  tft.resetViewport();
  tft.fillScreen(Palette::BG);
}

struct Layout {
  int16_t w = 0;
  int16_t h = 0;
  int16_t margin = 3;
  int16_t topY = 0;
  int16_t topH = 0;
  int16_t statsY = 0;
  int16_t statsH = 0;
  int16_t statusY = 0;
  int16_t statusH = 0;
  bool landscape = true;
};

static Layout layoutFor(TFT_eSPI &tft) {
  Layout l;
  l.w = tft.width();
  l.h = tft.height();
  l.landscape = (l.w >= l.h);
  l.margin = l.landscape ? 4 : 3;
  const int16_t avail = (int16_t)(l.h - l.margin * 4);
  const float topFrac = l.landscape ? 0.60f : 0.55f;
  const float statsFrac = l.landscape ? 0.22f : 0.24f;
  l.topH = (int16_t)(avail * topFrac);
  l.statsH = (int16_t)(avail * statsFrac);
  l.statusH = (int16_t)(avail - l.topH - l.statsH);
  l.topY = l.margin;
  l.statsY = (int16_t)(l.topY + l.topH + l.margin);
  l.statusY = (int16_t)(l.statsY + l.statsH + l.margin);
  return l;
}

static void drawHeaderBar(TFT_eSPI &tft,
                          int16_t x,
                          int16_t y,
                          int16_t w,
                          int16_t h,
                          const String &label,
                          uint16_t fg,
                          uint16_t bg,
                          bool showDot,
                          uint16_t dotCol) {
  tft.fillRect(x, y, w, h, bg);
  if (showDot) {
    const int16_t dotX = (int16_t)(x + 10);
    const int16_t dotY = (int16_t)(y + h / 2);
    tft.fillCircle(dotX, dotY, 4, dotCol);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fg, bg);
  tft.setTextFont(2);
  tft.drawString(label, (int16_t)(x + w / 2), (int16_t)(y + h / 2));
}

static int16_t pickLogoSize(int16_t panelW, int16_t maxLogo, int16_t padding) {
  const int16_t sizes[] = { 96, 64, 56, 48 };
  const int16_t minScoreArea = (panelW >= 300) ? 110 : 90;
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
    const int16_t s = sizes[i];
    if (s > maxLogo) continue;
    const int16_t scoreAreaW = (int16_t)(panelW - 2 * (s + padding));
    if (scoreAreaW >= minScoreArea) return s;
  }
  return (maxLogo < 48) ? maxLogo : 48;
}

// Forward declaration of drawScoreboardRow
static void drawScoreboardRow(TFT_eSPI &tft,
                              const TeamLine &away,
                              const TeamLine &home,
                              int16_t panelX,
                              int16_t panelW,
                              int16_t rowTop,
                              int16_t logoSize,
                              bool showAbbr,
                              bool showScores,
                              const String &midLabel);

static bool timeLooksValid() {
  // If SNTP has not set the clock, time(nullptr) will be close to 0.
  // Any value above 2020-01-01 is "good enough" for countdowns.
  return time(nullptr) > 1577836800;
}

static String fmtLocalTime(time_t epoch) {
  struct tm lt;
  localtime_r(&epoch, &lt);
  char buf[32];
  // Example: Tue 21 Jan 19:30
  strftime(buf, sizeof(buf), "%a %d %b %H:%M", &lt);
  return String(buf);
}

static String fmtLocalDate(time_t epoch) {
  struct tm lt;
  localtime_r(&epoch, &lt);
  char buf[24];
  // Example: 21 Jan 26
  strftime(buf, sizeof(buf), "%d %b %y", &lt);
  return String(buf);
}

static String fmtLocalClock(time_t epoch) {
  struct tm lt;
  localtime_r(&epoch, &lt);
  char buf[16];
  // Example: 19:30
  strftime(buf, sizeof(buf), "%H:%M", &lt);
  return String(buf);
}

static String fmtCountdown(int64_t seconds) {
  if (seconds < 0) seconds = 0;

  const int64_t days = seconds / 86400;
  seconds %= 86400;
  const int64_t hours = seconds / 3600;
  seconds %= 3600;
  const int64_t mins = seconds / 60;
  const int64_t secs = seconds % 60;

  char buf[32];
  if (days > 0) {
    // Dd HH:MM
    snprintf(buf, sizeof(buf), "%lldd %02lld:%02lld", (long long)days, (long long)hours, (long long)mins);
  } else {
    // HH:MM:SS
    snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", (long long)hours, (long long)mins, (long long)secs);
  }
  return String(buf);
}

static String elideText(const String &s, size_t maxLen) {
  if (maxLen < 4) return s;
  if (s.length() <= maxLen) return s;
  const int end = (int)maxLen - 3;
  if (end <= 0) return s;
  return s.substring(0, end) + "...";
}

static String elideToWidth(TFT_eSPI &tft, const String &s, int maxPx, int font) {
  if (maxPx <= 0) return s;
  if (tft.textWidth(s, font) <= maxPx) return s;
  String out = s;
  while (out.length() > 0 && tft.textWidth(out + "...", font) > maxPx) {
    out.remove(out.length() - 1);
  }
  if (out.length() == 0) return String("...");
  return out + "...";
}

static String fmtStatPair(int away, int home) {
  const String a = (away < 0) ? String("-") : String(away);
  const String h = (home < 0) ? String("-") : String(home);
  if (away < 0 && home < 0) return String("--");
  return a + "-" + h;
}

static String staleRightLabel(const GameState &g, const String &normal) {
  if (!g.wifiConnected) return String("OFFLINE");
  if (g.dataStale) return String("DATA STALE");
  return normal;
}

void Ui::begin(TFT_eSPI &tft, uint8_t rotation) {
  _tft = &tft;
  _rotation = (uint8_t)(rotation & 3);
  _tft->init();
  _tft->invertDisplay(false);
  // Use build-time rotation so portrait/landscape is consistent.
  // For ILI9341: 0/2 = portrait, 1/3 = landscape.
  _tft->setRotation(_rotation);
  _tft->resetViewport();
  _tft->fillScreen(Palette::BG);

  Serial.print("TFT rotation=");
  Serial.print(_rotation);
  Serial.print(" size=");
  Serial.print(_tft->width());
  Serial.print("x");
  Serial.println(_tft->height());

  resetCaches();
}

void Ui::setRotation(uint8_t rotation) {
  if (!_tft) return;
  _rotation = (uint8_t)(rotation & 3);
  _tft->setRotation(_rotation);
  clearScreenWithRotation(*_tft, _rotation);
  _hasLastMode = false;
  resetCaches();
}

void Ui::setBacklight(uint8_t pct) {
  ledcWrite(CYD_BL_PWM_CH, map(pct, 0, 100, 0, 255));
}

void Ui::drawBootSplash(const String &line1, const String &line2) {
  if (!_tft) return;
  clearScreenWithRotation(*_tft, _rotation);
  drawFrame();

  const int16_t W = _tft->width();
  const int16_t H = _tft->height();

  // Prefer SPIFFS splash when present; fallback to vector splash if missing/invalid.
  if (Assets::drawPng(*_tft, "/splash.png", 0, 0)) {
    if (line2.length()) {
      const int16_t bandH = 18;
      _tft->fillRect(1, (int16_t)(H - bandH - 1), (int16_t)(W - 2), bandH, Palette::BG);
      drawCentered(*_tft, line2, W / 2, (int16_t)(H - bandH / 2 - 1), 2, Palette::WHITE, Palette::BG);
    }
    return;
  }

  // Fallback vector splash: Canada flag + Olympic rings.
  _tft->fillRect(0, 0, W, 24, Palette::PANEL_2);
  drawCentered(*_tft, "CANADIAN WOMEN'S ICE HOCKEY TEAM", W / 2, 12, 2, Palette::WHITE, Palette::PANEL_2);

  const int16_t fx = (int16_t)(W / 2 - 66);
  const int16_t fy = 42;
  const int16_t fw = 132;
  const int16_t fh = 82;
  _tft->fillRect(fx, fy, fw, fh, Palette::WHITE);
  _tft->fillRect(fx, fy, 32, fh, TFT_RED);
  _tft->fillRect((int16_t)(fx + fw - 32), fy, 32, fh, TFT_RED);
  _tft->fillTriangle((int16_t)(fx + fw / 2), (int16_t)(fy + 22),
                     (int16_t)(fx + fw / 2 - 14), (int16_t)(fy + 54),
                     (int16_t)(fx + fw / 2 + 14), (int16_t)(fy + 54), TFT_RED);
  _tft->fillRect((int16_t)(fx + fw / 2 - 4), (int16_t)(fy + 54), 8, 16, TFT_RED);

  const int16_t ringsY = (int16_t)(fy + fh + 42);
  const int16_t ringR = 14;
  const int16_t gap = 34;
  const int16_t rx = (int16_t)(W / 2 - (2 * gap));
  _tft->drawCircle(rx, ringsY, ringR, TFT_BLUE);
  _tft->drawCircle((int16_t)(rx + gap), ringsY, ringR, TFT_BLACK);
  _tft->drawCircle((int16_t)(rx + 2 * gap), ringsY, ringR, TFT_RED);
  _tft->drawCircle((int16_t)(rx + gap / 2), (int16_t)(ringsY + 12), ringR, TFT_YELLOW);
  _tft->drawCircle((int16_t)(rx + gap + gap / 2), (int16_t)(ringsY + 12), ringR, TFT_GREEN);

  if (line1.length()) {
    drawCentered(*_tft, line1, W / 2, (int16_t)(H - 28), 2, Palette::GREY, Palette::BG);
  }
  if (line2.length()) {
    drawCentered(*_tft, line2, W / 2, (int16_t)(H - 12), 2, Palette::GREY, Palette::BG);
  }
}

bool Ui::ensureScreen(ScreenMode mode) {
  if (!_hasLastMode || _lastMode != mode) {
    clearScreenWithRotation(*_tft, _rotation);
    drawFrame();
    _lastMode = mode;
    _hasLastMode = true;
    return true;
  }
  return false;
}

void Ui::resetCaches() {
  _liveScore.valid = false;
  _liveStats.valid = false;
  _liveStatus.valid = false;

  _interScore.valid = false;
  _interStats.valid = false;
  _interStatus.valid = false;

  _finalScore.valid = false;
  _finalStats.valid = false;
  _finalStatus.valid = false;

  _preScore.valid = false;
  _preStatus.valid = false;
  _preInfo.valid = false;

  _lastGameKey = "";
  _noGameKey = "";
  _preKey = "";
  _standingsKey = "";
  _countdownKey = "";
  _countdownValue = "";
  _countdownDate = "";
  _countdownLocation = "";
}

void Ui::drawFrame() {
  if (!_tft) return;
  _tft->drawRect(0, 0, _tft->width(), _tft->height(), Palette::FRAME);
}

void Ui::framePanel(int16_t x, int16_t y, int16_t w, int16_t h) {
  _tft->fillRect(x, y, w, h, Palette::PANEL);
  _tft->drawRect(x, y, w, h, Palette::PANEL_2);
}

void Ui::drawTopScorePanel(const GameState &g,
                           const String &label,
                           bool showScores,
                           const String &midLabel) {
  const Layout l = layoutFor(*_tft);
  const int16_t x = l.margin;
  const int16_t y = l.topY;
  const int16_t w = (int16_t)(l.w - l.margin * 2);
  const int16_t h = l.topH;

  framePanel(x, y, w, h);

  const int16_t barH = l.landscape ? 20 : 18;
  const bool showDot = (label == "LIVE");
  drawHeaderBar(*_tft, (int16_t)(x + 1), (int16_t)(y + 1), (int16_t)(w - 2), barH,
                label, Palette::WHITE, Palette::PANEL_2, showDot, Palette::GOLD);

  const int16_t padding = (w >= 300) ? 6 : 5;
  const int16_t maxLogo = (int16_t)(h - barH - 12);
  const int16_t logoSize = pickLogoSize(w, maxLogo, padding);
  const int16_t rowTop = (int16_t)(y + barH + ((h - barH - logoSize) / 2));

  drawScoreboardRow(*_tft,
                    g.home,
                    g.away,
                    x,
                    w,
                    rowTop,
                    logoSize,
                    true,
                    showScores,
                    midLabel);
}

void Ui::drawStatsBand(const GameState &g) {
  const Layout l = layoutFor(*_tft);
  const int16_t x = l.margin;
  const int16_t y = l.statsY;
  const int16_t w = (int16_t)(l.w - l.margin * 2);
  const int16_t h = l.statsH;

  framePanel(x, y, w, h);

  const int16_t colW = (int16_t)(w / 3);
  const int16_t labelY = (int16_t)(y + 6);
  const int16_t valueY = (int16_t)(y + h / 2 + 6);

  _tft->setTextFont(2);
  _tft->setTextDatum(MC_DATUM);
  _tft->setTextColor(Palette::GREY, Palette::PANEL);

  _tft->drawString("SOG", (int16_t)(x + colW / 2), labelY);
  _tft->drawString("HITS", (int16_t)(x + colW + colW / 2), labelY);
  _tft->drawString("FO%", (int16_t)(x + 2 * colW + colW / 2), labelY);

  const int16_t valueFont = (h >= 48) ? 4 : 2;
  _tft->setTextFont(valueFont);
  _tft->setTextColor(Palette::WHITE, Palette::PANEL);

  _tft->drawString(fmtStatPair(g.away.sog, g.home.sog), (int16_t)(x + colW / 2), valueY);
  _tft->drawString(fmtStatPair(g.away.hits, g.home.hits), (int16_t)(x + colW + colW / 2), valueY);
  _tft->drawString(fmtStatPair(g.away.foPct, g.home.foPct), (int16_t)(x + 2 * colW + colW / 2), valueY);
}

void Ui::drawStatusBar(const String &left,
                       const String &right,
                       uint16_t dotCol,
                       bool showDot) {
  const Layout l = layoutFor(*_tft);
  const int16_t x = l.margin;
  const int16_t y = l.statusY;
  const int16_t w = (int16_t)(l.w - l.margin * 2);
  const int16_t h = l.statusH;

  framePanel(x, y, w, h);

  const int16_t midY = (int16_t)(y + h / 2);
  if (showDot) {
    const int16_t dotX = (int16_t)(x + 10);
    _tft->fillCircle(dotX, midY, 4, dotCol);
  }

  _tft->setTextColor(Palette::WHITE, Palette::PANEL);

  const int16_t valueFont = (h >= 48) ? 4 : 2;
  _tft->setTextFont(valueFont);
  _tft->setTextDatum(ML_DATUM);
  _tft->drawString(left, (int16_t)(x + 20), midY);

  _tft->setTextFont(2);
  _tft->setTextDatum(MR_DATUM);
  _tft->drawString(right, (int16_t)(x + w - 8), midY);
}

struct NextGameView {
  String leftAbbr;
  String rightAbbr;
  String leftLogoUrl;
  String rightLogoUrl;
  time_t startEpoch = 0;
  String venue;
  String city;
  bool gameDay = false;
  String groupSummary;
};

struct NextGameLayout {
  int16_t logoSize = 0;
  int16_t logoPad = 0;
  int16_t leftLogoX = 0;
  int16_t rightLogoX = 0;
  int16_t rowY = 0;
  int16_t abbrY = 0;
  int16_t seasonY = 0;
  int16_t titleY = 0;
  int16_t countdownY = 0;
  int16_t infoY1 = 0;
  int16_t infoY2 = 0;
  int16_t centerLeft = 0;
  int16_t centerW = 0;
  int16_t countdownBoxH = 0;
  int16_t infoTop = 0;
  int16_t infoH = 0;
  int16_t countdownFont = 0;
};

static NextGameLayout nextGameLayoutFor(const Layout &l) {
  NextGameLayout ng;
  const bool wide = (l.w >= 300);
  ng.logoSize = wide ? 64 : 56;
  ng.logoPad = wide ? 12 : 8;
  ng.countdownFont = wide ? 4 : 2;
  ng.countdownBoxH = wide ? 32 : 20;

  const int16_t titleH = wide ? 40 : 36;
  const int16_t gap1 = wide ? 8 : 6;
  const int16_t rowH = (int16_t)(ng.logoSize + 16);
  const int16_t gap2 = wide ? 8 : 6;
  const int16_t infoH = 32;

  int16_t contentH = (int16_t)(titleH + gap1 + rowH + gap2 + infoH);
  int16_t startY = (int16_t)((l.h - contentH) / 2);
  if (startY < l.margin) startY = l.margin;

  ng.seasonY = (int16_t)(startY + 8);
  ng.titleY = (int16_t)(startY + 30);
  ng.rowY = (int16_t)(startY + titleH + gap1);
  ng.countdownY = (int16_t)(ng.rowY + ng.logoSize / 2 + 2);
  ng.abbrY = (int16_t)(ng.rowY + ng.logoSize + 10);
  ng.infoY1 = (int16_t)(ng.rowY + rowH + gap2 + 6);
  ng.infoY2 = (int16_t)(ng.infoY1 + 16);
  ng.infoTop = (int16_t)(ng.infoY1 - 10);
  ng.infoH = (int16_t)((ng.infoY2 - ng.infoY1) + 20);

  ng.leftLogoX = (int16_t)(l.margin + ng.logoPad);
  ng.rightLogoX = (int16_t)(l.w - l.margin - ng.logoPad - ng.logoSize);

  ng.centerLeft = (int16_t)(ng.leftLogoX + ng.logoSize + ng.logoPad);
  const int16_t centerRight = (int16_t)(ng.rightLogoX - ng.logoPad);
  ng.centerW = (int16_t)(centerRight - ng.centerLeft);
  if (ng.centerW < 0) ng.centerW = 0;

  return ng;
}

static String buildCanadaGroupSummary(const GameState &g) {
  if (g.standings.canadaGroup == '?' || g.standings.canadaRank < 1) return String("");
  String line = "Group ";
  line += g.standings.canadaGroup;
  line += ": CAN #";
  line += String((int)g.standings.canadaRank);
  line += ", ";
  line += String(g.standings.canadaPts);
  line += " pts";
  if (g.standings.usedRegulationFallback) line += "*";
  return line;
}

static bool buildNextGameView(const GameState &g, const String &focusTeamAbbr, NextGameView &out) {
  out.groupSummary = buildCanadaGroupSummary(g);

  if (g.hasNextGame && g.nextOppAbbr.length()) {
    if (g.nextIsHome) {
      out.leftAbbr = focusTeamAbbr;
      out.rightAbbr = g.nextOppAbbr;
      out.leftLogoUrl = g.nextFocusLogoUrl;
      out.rightLogoUrl = g.nextOppLogoUrl;
    } else {
      out.leftAbbr = g.nextOppAbbr;
      out.rightAbbr = focusTeamAbbr;
      out.leftLogoUrl = g.nextOppLogoUrl;
      out.rightLogoUrl = g.nextFocusLogoUrl;
    }
    out.startEpoch = g.nextStartEpoch;
    out.venue = g.nextVenue;
    out.city = g.nextCity;
  } else if (g.hasGame && g.isPre && g.away.abbr.length() && g.home.abbr.length()) {
    out.leftAbbr = g.home.abbr;
    out.rightAbbr = g.away.abbr;
    out.leftLogoUrl = g.home.logoUrl;
    out.rightLogoUrl = g.away.logoUrl;
    out.startEpoch = (g.nextStartEpoch > 0) ? g.nextStartEpoch : g.startEpoch;
    out.venue = g.nextVenue.length() ? g.nextVenue : g.statusDetail;
    out.city = g.nextCity;
  } else {
    return false;
  }

  out.gameDay = false;
  if (out.startEpoch > 0 && timeLooksValid()) {
    const int64_t seconds = (int64_t)difftime(out.startEpoch, time(nullptr));
    out.gameDay = (seconds >= 0 && seconds <= 6LL * 3600LL);
  }

  return true;
}

static void drawCountdownScreen(TFT_eSPI &tft,
                                const Layout &l,
                                const NextGameView &view,
                                const GameState &g,
                                bool fullRedraw,
                                const char *title,
                                const char *subtitle,
                                const char *dateLabel,
                                String *countdownCache,
                                String *dateCache,
                                String *locationCache) {
  const NextGameLayout ng = nextGameLayoutFor(l);

  tft.setTextDatum(MC_DATUM);

  if (fullRedraw) {
    String subtitleLine = subtitle ? String(subtitle) : String("");
    if (view.gameDay) subtitleLine = "GAME DAY | WOMEN'S TOURNAMENT";
    if (subtitleLine.length()) {
      tft.setTextColor(Palette::GREY, Palette::BG);
      tft.setTextFont(2);
      tft.drawString(subtitleLine, (int16_t)(l.w / 2), ng.seasonY);
    }

    tft.setTextColor(Palette::WHITE, Palette::BG);
    tft.setTextFont(4);
    tft.drawString(title ? title : "NEXT GAME", (int16_t)(l.w / 2), ng.titleY);

    if (view.leftAbbr.length()) {
      Assets::drawLogo(tft, view.leftAbbr, view.leftLogoUrl, ng.leftLogoX, ng.rowY, ng.logoSize);
    }
    if (view.rightAbbr.length()) {
      Assets::drawLogo(tft, view.rightAbbr, view.rightLogoUrl, ng.rightLogoX, ng.rowY, ng.logoSize);
    }

    tft.setTextColor(Palette::GREY, Palette::BG);
    tft.setTextFont(2);
    if (view.leftAbbr.length()) {
      tft.drawString(view.leftAbbr, (int16_t)(ng.leftLogoX + ng.logoSize / 2), ng.abbrY);
    }
    if (view.rightAbbr.length()) {
      tft.drawString(view.rightAbbr, (int16_t)(ng.rightLogoX + ng.logoSize / 2), ng.abbrY);
    }
  }

  const String staleLabel = !g.wifiConnected ? String("OFFLINE") : (g.dataStale ? String("DATA STALE") : String(""));
  const int16_t badgeW = (l.w >= 300) ? 110 : 92;
  const int16_t badgeH = 16;
  const int16_t badgeX = (int16_t)(l.w - l.margin - badgeW);
  const int16_t badgeY = (int16_t)(l.margin + 2);
  tft.fillRect(badgeX, badgeY, badgeW, badgeH, Palette::BG);
  if (staleLabel.length()) {
    tft.setTextDatum(MR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(Palette::GREY, Palette::BG);
    tft.drawString(staleLabel, (int16_t)(l.w - l.margin - 2), (int16_t)(badgeY + badgeH / 2));
    tft.setTextDatum(MC_DATUM);
  }

  String countdown = "--:--:--";
  if (view.startEpoch > 0 && timeLooksValid()) {
    const int64_t seconds = (int64_t)difftime(view.startEpoch, time(nullptr));
    countdown = fmtCountdown(seconds);
  }

  String dateLine = String(dateLabel && dateLabel[0] ? dateLabel : "GAME DATE");
  dateLine += ": ";
  if (view.startEpoch > 0) {
    dateLine += fmtLocalDate(view.startEpoch);
    dateLine += " ";
    dateLine += fmtLocalClock(view.startEpoch);
  } else {
    dateLine += "TBA";
  }
  const size_t maxDateLen = (l.w >= 300) ? 28 : 24;
  dateLine = elideText(dateLine, maxDateLen);

  String location;
  if (view.venue.length()) {
    location = view.venue;
    if (view.city.length()) {
      location += " - ";
      location += view.city;
    }
  } else if (view.city.length()) {
    location = view.city;
  } else {
    location = "TBA";
  }
  String locationLine = String("LOCATION: ") + location;
  const size_t maxLocLen = (l.w >= 300) ? 28 : 24;
  locationLine = elideText(locationLine, maxLocLen);

  bool infoChanged = fullRedraw;
  if (dateCache && *dateCache != dateLine) infoChanged = true;
  if (locationCache && *locationCache != locationLine) infoChanged = true;

  if (infoChanged) {
    tft.fillRect(l.margin, ng.infoTop, (int16_t)(l.w - l.margin * 2), ng.infoH, Palette::BG);
    tft.setTextFont(2);
    tft.setTextColor(Palette::WHITE, Palette::BG);
    tft.drawString(dateLine, (int16_t)(l.w / 2), ng.infoY1);
    tft.setTextColor(Palette::GREY, Palette::BG);
    tft.drawString(locationLine, (int16_t)(l.w / 2), ng.infoY2);
    if (dateCache) *dateCache = dateLine;
    if (locationCache) *locationCache = locationLine;
  }

  if (fullRedraw || infoChanged) {
    const bool canFitMiniTable = (l.h >= 270);
    if (canFitMiniTable && g.standings.canadaGroup != '?') {
      tft.fillRect(l.margin, (int16_t)(l.h - 62), (int16_t)(l.w - l.margin * 2), 58, Palette::BG);

      const GroupStandings *group = nullptr;
      for (uint8_t gi = 0; gi < g.standings.groupCount; ++gi) {
        if (g.standings.groups[gi].group == g.standings.canadaGroup) {
          group = &g.standings.groups[gi];
          break;
        }
      }

      if (group) {
        tft.setTextFont(1);
        tft.setTextColor(Palette::GREY, Palette::BG);
        String gLabel = "GROUP ";
        gLabel += group->group;
        tft.drawString(gLabel, (int16_t)(l.w / 2), (int16_t)(l.h - 56));
        tft.drawString("TM W OTW OTL L PTS", (int16_t)(l.w / 2), (int16_t)(l.h - 46));

        const uint8_t rows = (group->rowCount < 4) ? group->rowCount : 4;
        for (uint8_t r = 0; r < rows; ++r) {
          const StandingsRow &row = group->rows[r];
          String line = row.abbr + " " + String(row.w) + " " + String(row.otw) + " " +
                        String(row.otl) + " " + String(row.l) + " " + String(row.pts);
          const int16_t y = (int16_t)(l.h - 35 + r * 10);
          tft.setTextColor((row.abbr == FOCUS_TEAM_ABBR) ? Palette::WHITE : Palette::GREY, Palette::BG);
          tft.drawString(line, (int16_t)(l.w / 2), y);
        }
      }
    } else {
      tft.fillRect(l.margin, (int16_t)(l.h - 18), (int16_t)(l.w - l.margin * 2), 14, Palette::BG);
      if (view.groupSummary.length()) {
        tft.setTextFont(2);
        tft.setTextColor(Palette::GREY, Palette::BG);
        tft.drawString(elideText(view.groupSummary, (l.w >= 300) ? 30 : 24), (int16_t)(l.w / 2), (int16_t)(l.h - 10));
      }
    }
  }

  if (!countdownCache || *countdownCache != countdown) {
    if (ng.centerW > 0) {
      tft.fillRect(ng.centerLeft,
                   (int16_t)(ng.countdownY - ng.countdownBoxH / 2),
                   ng.centerW,
                   ng.countdownBoxH,
                   Palette::BG);
    }
    tft.setTextColor(Palette::WHITE, Palette::BG);
    int16_t countdownFont = ng.countdownFont;
    if (countdown.length() > 8 && countdownFont > 2) {
      countdownFont = 2;
    }
    tft.setTextFont(countdownFont);
    tft.drawString(countdown, (int16_t)(l.w / 2), ng.countdownY);
    if (countdownCache) *countdownCache = countdown;
  }
}

// -----------------------------------------------------------------------------
// NO GAME
// -----------------------------------------------------------------------------

void Ui::drawNextGame(const GameState &g, const String &focusTeamAbbr) {
  const bool modeChanged = ensureScreen(ScreenMode::NEXT_GAME);
  NextGameView view;
  const bool hasNext = buildNextGameView(g, focusTeamAbbr, view);
  const String key = hasNext ? (view.leftAbbr + "|" + view.rightAbbr) : String("NONE");
  bool fullRedraw = modeChanged;
  if (key != _noGameKey) {
    _noGameKey = key;
    fullRedraw = true;
  }
  if (fullRedraw && !modeChanged) {
    clearScreenWithRotation(*_tft, _rotation);
    drawFrame();
  }

  const Layout l = layoutFor(*_tft);

  if (hasNext) {
    if (_countdownKey != key) {
      _countdownKey = key;
      _countdownValue = "";
      _countdownDate = "";
      _countdownLocation = "";
      fullRedraw = true;
    }
    drawCountdownScreen(*_tft, l, view, g, fullRedraw, "NEXT CANADA GAME", "2026 OLYMPICS | WOMEN'S TOURNAMENT", "PUCK DROP",
                        &_countdownValue, &_countdownDate, &_countdownLocation);
  } else if (fullRedraw) {
    const int16_t panelX2 = l.margin;
    const int16_t panelW2 = (int16_t)(l.w - l.margin * 2);
    framePanel(panelX2, l.topY, panelW2, l.topH);
    drawCentered(*_tft, "NO CANADA GAME", l.w / 2, (int16_t)(l.topY + l.topH / 2 - 10), 4, Palette::WHITE, Palette::PANEL);
    drawCentered(*_tft, "CHECKING WOMEN'S FEED", l.w / 2, (int16_t)(l.topY + l.topH / 2 + 18), 2, Palette::GREY, Palette::PANEL);
    framePanel(panelX2, l.statsY, panelW2, l.statsH);
    drawCentered(*_tft, "CONNECTING...", l.w / 2, (int16_t)(l.statsY + l.statsH / 2), 2, Palette::WHITE, Palette::PANEL);
    framePanel(panelX2, l.statusY, panelW2, l.statusH);
  }
}

// -----------------------------------------------------------------------------
// LAST GAME RECAP
// -----------------------------------------------------------------------------

static String formatScorer(const ScorerEntry &entry) {
  if (entry.name.isEmpty()) return String("-");
  if (entry.goals > 1) {
    return entry.name + " (" + String(entry.goals) + ")";
  }
  return entry.name;
}

static String buildPeriodLine(const LastGameRecap &recap, int16_t w) {
  if (!recap.periodCount) return String("PERIODS: TBA");
  String line;
  for (uint8_t i = 0; i < recap.periodCount; ++i) {
    if (i) line += "  ";
    line += recap.periods[i].label;
    line += " ";
    line += String(recap.periods[i].home);
    line += "-";
    line += String(recap.periods[i].away);
  }
  const size_t maxLen = (w >= 300) ? 32 : 26;
  return elideText(line, maxLen);
}

void Ui::drawLastGame(const GameState &g) {
  const bool modeChanged = ensureScreen(ScreenMode::LAST_GAME);
  const String key = g.last.hasGame ? g.last.gameId : String("NONE");
  bool fullRedraw = modeChanged || key != _lastGameKey;
  if (fullRedraw && !modeChanged) {
    clearScreenWithRotation(*_tft, _rotation);
    drawFrame();
  }
  _lastGameKey = key;

  const Layout l = layoutFor(*_tft);
  const int16_t x = l.margin;
  const int16_t w = (int16_t)(l.w - l.margin * 2);

  // Top score panel
  framePanel(x, l.topY, w, l.topH);
  const int16_t barH = l.landscape ? 20 : 18;
  drawHeaderBar(*_tft, (int16_t)(x + 1), (int16_t)(l.topY + 1), (int16_t)(w - 2), barH,
                "LAST GAME", Palette::WHITE, Palette::PANEL_2, false, Palette::GOLD);

  if (!g.last.hasGame) {
    drawCentered(*_tft, "NO RECENT GAME", l.w / 2, (int16_t)(l.topY + l.topH / 2), 4, Palette::WHITE, Palette::PANEL);
    return;
  }

  const int16_t padding = (w >= 300) ? 6 : 5;
  const int16_t maxLogo = (int16_t)(l.topH - barH - 12);
  const int16_t logoSize = pickLogoSize(w, maxLogo, padding);
  const int16_t rowTop = (int16_t)(l.topY + barH + ((l.topH - barH - logoSize) / 2));

  // drawScoreboardRow expects left=away; pass home first so home is on the left.
  drawScoreboardRow(*_tft,
                    g.last.home,
                    g.last.away,
                    x,
                    w,
                    rowTop,
                    logoSize,
                    true,
                    true,
                    "-");

  // Scorers panel
  framePanel(x, l.statsY, w, l.statsH);
  _tft->setTextDatum(MC_DATUM);
  _tft->setTextFont(2);
  _tft->setTextColor(Palette::GREY, Palette::PANEL);
  _tft->drawString("SCORERS", l.w / 2, (int16_t)(l.statsY + 8));

  const int16_t colPad = 10;
  const int16_t leftX = (int16_t)(x + colPad);
  const int16_t rightX = (int16_t)(x + w - colPad);
  const int16_t listTop = (int16_t)(l.statsY + 18);
  const int16_t listBottom = (int16_t)(l.statsY + l.statsH - 6);
  const int16_t listH = (int16_t)(listBottom - listTop);
  const uint8_t maxLines = (listH >= 50) ? 3 : 2;
  const int16_t lineH = (maxLines > 0) ? (int16_t)(listH / maxLines) : 16;
  const int16_t startY = (int16_t)(listTop + lineH / 2);
  const int16_t colW = (int16_t)(w / 2 - colPad * 2);
  const int scorerFont = (lineH < 16) ? 1 : 2;

  _tft->setTextColor(Palette::WHITE, Palette::PANEL);
  _tft->setTextFont(scorerFont);

  for (uint8_t i = 0; i < maxLines; ++i) {
    String leftLine = "-";
    if (i < g.last.homeScorerCount) leftLine = formatScorer(g.last.homeScorers[i]);
    leftLine = elideToWidth(*_tft, leftLine, colW, scorerFont);
    String rightLine = "-";
    if (i < g.last.awayScorerCount) rightLine = formatScorer(g.last.awayScorers[i]);
    rightLine = elideToWidth(*_tft, rightLine, colW, scorerFont);
    const int16_t y = (int16_t)(startY + i * lineH);
    _tft->setTextDatum(ML_DATUM);
    _tft->drawString(leftLine, leftX, y);
    _tft->setTextDatum(MR_DATUM);
    _tft->drawString(rightLine, rightX, y);
  }
  _tft->setTextDatum(MC_DATUM);

  // Period stats panel
  framePanel(x, l.statusY, w, l.statusH);
  String periodLine = buildPeriodLine(g.last, l.w);
  _tft->setTextColor(Palette::WHITE, Palette::PANEL);
  _tft->setTextFont(2);
  _tft->drawString(periodLine, l.w / 2, (int16_t)(l.statusY + l.statusH / 2));
}

// -----------------------------------------------------------------------------
// GAME SCREENS
// -----------------------------------------------------------------------------

void Ui::drawNoGame(const GameState &g, const String &focusTeamAbbr) {
  drawNextGame(g, focusTeamAbbr);
}

void Ui::drawPregame(const GameState &g, const String &focusTeamAbbr) {
  drawNextGame(g, focusTeamAbbr);
}

void Ui::drawLive(const GameState &g) {
  const bool modeChanged = ensureScreen(ScreenMode::LIVE);

  bool scoreChanged = modeChanged || !_liveScore.valid
    || _liveScore.homeAbbr != g.home.abbr
    || _liveScore.awayAbbr != g.away.abbr
    || _liveScore.homeScore != g.home.score
    || _liveScore.awayScore != g.away.score;
  if (scoreChanged) {
    drawTopScorePanel(g, "LIVE", true, "-");
    _liveScore.valid = true;
    _liveScore.homeAbbr = g.home.abbr;
    _liveScore.awayAbbr = g.away.abbr;
    _liveScore.homeScore = g.home.score;
    _liveScore.awayScore = g.away.score;
  }

  bool statsChanged = modeChanged || !_liveStats.valid
    || _liveStats.homeSog != g.home.sog
    || _liveStats.awaySog != g.away.sog
    || _liveStats.homeHits != g.home.hits
    || _liveStats.awayHits != g.away.hits
    || _liveStats.homeFo != g.home.foPct
    || _liveStats.awayFo != g.away.foPct;
  if (statsChanged) {
    drawStatsBand(g);
    _liveStats.valid = true;
    _liveStats.homeSog = g.home.sog;
    _liveStats.awaySog = g.away.sog;
    _liveStats.homeHits = g.home.hits;
    _liveStats.awayHits = g.away.hits;
    _liveStats.homeFo = g.home.foPct;
    _liveStats.awayFo = g.away.foPct;
  }

  String clockLine = g.clock.length() ? g.clock : String("IN PLAY");
  if (g.period > 0) {
    clockLine += "  P";
    clockLine += String(g.period);
  }
  String strength = g.strengthLabel.length() ? g.strengthLabel : String("EVEN STRENGTH");
  strength = staleRightLabel(g, strength);

  const bool statusChanged = modeChanged || !_liveStatus.valid
    || _liveStatus.left != clockLine
    || _liveStatus.right != strength
    || _liveStatus.showDot != true
    || _liveStatus.dotCol != Palette::STATUS_PK;
  if (statusChanged) {
    drawStatusBar(clockLine, strength, Palette::STATUS_PK, true);
    _liveStatus.valid = true;
    _liveStatus.left = clockLine;
    _liveStatus.right = strength;
    _liveStatus.dotCol = Palette::STATUS_PK;
    _liveStatus.showDot = true;
  }
}

void Ui::drawIntermission(const GameState &g) {
  const bool modeChanged = ensureScreen(ScreenMode::INTERMISSION);

  bool scoreChanged = modeChanged || !_interScore.valid
    || _interScore.homeAbbr != g.home.abbr
    || _interScore.awayAbbr != g.away.abbr
    || _interScore.homeScore != g.home.score
    || _interScore.awayScore != g.away.score;
  if (scoreChanged) {
    drawTopScorePanel(g, "INTERMISSION", true, "-");
    _interScore.valid = true;
    _interScore.homeAbbr = g.home.abbr;
    _interScore.awayAbbr = g.away.abbr;
    _interScore.homeScore = g.home.score;
    _interScore.awayScore = g.away.score;
  }

  bool statsChanged = modeChanged || !_interStats.valid
    || _interStats.homeSog != g.home.sog
    || _interStats.awaySog != g.away.sog
    || _interStats.homeHits != g.home.hits
    || _interStats.awayHits != g.away.hits
    || _interStats.homeFo != g.home.foPct
    || _interStats.awayFo != g.away.foPct;
  if (statsChanged) {
    drawStatsBand(g);
    _interStats.valid = true;
    _interStats.homeSog = g.home.sog;
    _interStats.awaySog = g.away.sog;
    _interStats.homeHits = g.home.hits;
    _interStats.awayHits = g.away.hits;
    _interStats.homeFo = g.home.foPct;
    _interStats.awayFo = g.away.foPct;
  }

  String left = "INTERMISSION";
  if (g.period > 0) {
    left = "END P";
    left += String(g.period);
  }
  String right = staleRightLabel(g, "BREAK");

  const bool statusChanged = modeChanged || !_interStatus.valid
    || _interStatus.left != left
    || _interStatus.right != right
    || _interStatus.showDot != false
    || _interStatus.dotCol != Palette::STATUS_EVEN;
  if (statusChanged) {
    drawStatusBar(left, right, Palette::STATUS_EVEN, false);
    _interStatus.valid = true;
    _interStatus.left = left;
    _interStatus.right = right;
    _interStatus.dotCol = Palette::STATUS_EVEN;
    _interStatus.showDot = false;
  }
}

void Ui::drawFinal(const GameState &g) {
  const bool modeChanged = ensureScreen(ScreenMode::FINAL);

  bool scoreChanged = modeChanged || !_finalScore.valid
    || _finalScore.homeAbbr != g.home.abbr
    || _finalScore.awayAbbr != g.away.abbr
    || _finalScore.homeScore != g.home.score
    || _finalScore.awayScore != g.away.score;
  if (scoreChanged) {
    drawTopScorePanel(g, "FINAL", true, "-");
    _finalScore.valid = true;
    _finalScore.homeAbbr = g.home.abbr;
    _finalScore.awayAbbr = g.away.abbr;
    _finalScore.homeScore = g.home.score;
    _finalScore.awayScore = g.away.score;
  }

  bool statsChanged = modeChanged || !_finalStats.valid
    || _finalStats.homeSog != g.home.sog
    || _finalStats.awaySog != g.away.sog
    || _finalStats.homeHits != g.home.hits
    || _finalStats.awayHits != g.away.hits
    || _finalStats.homeFo != g.home.foPct
    || _finalStats.awayFo != g.away.foPct;
  if (statsChanged) {
    drawStatsBand(g);
    _finalStats.valid = true;
    _finalStats.homeSog = g.home.sog;
    _finalStats.awaySog = g.away.sog;
    _finalStats.homeHits = g.home.hits;
    _finalStats.awayHits = g.away.hits;
    _finalStats.homeFo = g.home.foPct;
    _finalStats.awayFo = g.away.foPct;
  }

  String right = staleRightLabel(g, "FULL TIME");
  const bool statusChanged = modeChanged || !_finalStatus.valid
    || _finalStatus.left != "FINAL"
    || _finalStatus.right != right
    || _finalStatus.showDot != false
    || _finalStatus.dotCol != Palette::STATUS_EVEN;
  if (statusChanged) {
    drawStatusBar("FINAL", right, Palette::STATUS_EVEN, false);
    _finalStatus.valid = true;
    _finalStatus.left = "FINAL";
    _finalStatus.right = right;
    _finalStatus.dotCol = Palette::STATUS_EVEN;
    _finalStatus.showDot = false;
  }
}

void Ui::drawGoal(const GameState &g) {
  ensureScreen(ScreenMode::GOAL);
  const uint16_t bg = g.focusJustScored ? Palette::FOCUS_BLUE : Palette::PANEL_2;
  _tft->fillScreen(bg);

  drawCentered(*_tft, "GOAL!", _tft->width() / 2, 54, 4, Palette::WHITE, bg);

  if (g.goalTeamAbbr.length()) {
    const int16_t logoSize = 96;
    const int16_t logoX = (int16_t)(_tft->width() / 2 - logoSize / 2);
    const int16_t logoY = 78;
    Assets::drawLogo(*_tft, g.goalTeamAbbr, g.goalTeamLogoUrl, logoX, logoY, logoSize);
  }

  const int16_t textWidth = (int16_t)(_tft->width() - 16);
  if (g.goalScorer.length()) {
    String scorerLine = elideToWidth(*_tft, g.goalScorer, textWidth, 2);
    drawCentered(*_tft, scorerLine, _tft->width() / 2, 186, 2, Palette::WHITE, bg);
  }

  if (g.goalText.length()) {
    String detailLine = elideToWidth(*_tft, g.goalText, textWidth, 2);
    drawCentered(*_tft, detailLine, _tft->width() / 2, 206, 2, Palette::WHITE, bg);
  }
}

void Ui::drawStandings(const GameState &g, const String &focusTeamAbbr) {
  (void)ensureScreen(ScreenMode::STANDINGS);
  clearScreenWithRotation(*_tft, _rotation);
  drawFrame();

  const int16_t w = _tft->width();
  const int16_t h = _tft->height();

  _tft->fillRect(0, 0, w, 22, Palette::PANEL_2);
  _tft->setTextDatum(MC_DATUM);
  _tft->setTextColor(Palette::WHITE, Palette::PANEL_2);
  _tft->setTextFont(2);
  _tft->drawString("GROUP STANDINGS", w / 2, 11);

  if (g.standings.groupCount == 0) {
    _tft->setTextColor(Palette::WHITE, Palette::BG);
    _tft->setTextFont(4);
    _tft->drawString("NO STANDINGS", w / 2, h / 2 - 8);
    _tft->setTextColor(Palette::GREY, Palette::BG);
    _tft->setTextFont(2);
    _tft->drawString("Waiting for completed group games", w / 2, h / 2 + 16);
    return;
  }

  const int16_t top = 24;
  const int16_t usableH = (int16_t)(h - top - 2);
  const int16_t sectionH = (int16_t)(usableH / g.standings.groupCount);

  for (uint8_t gi = 0; gi < g.standings.groupCount; ++gi) {
    const GroupStandings &group = g.standings.groups[gi];
    const int16_t y = (int16_t)(top + gi * sectionH);
    const int16_t secH = (gi == g.standings.groupCount - 1) ? (int16_t)(h - y - 1) : sectionH;

    _tft->fillRect(2, y, w - 4, secH - 1, Palette::PANEL);
    _tft->drawRect(2, y, w - 4, secH - 1, Palette::PANEL_2);

    _tft->setTextColor(Palette::WHITE, Palette::PANEL);
    _tft->setTextFont(2);
    String title = "GROUP ";
    title += group.group;
    _tft->drawString(title, 36, (int16_t)(y + 9));

    _tft->setTextColor(Palette::GREY, Palette::PANEL);
    _tft->setTextFont(1);
    _tft->drawString("TM", 18, (int16_t)(y + 22));
    _tft->drawString("W", 92, (int16_t)(y + 22));
    _tft->drawString("OTW", 124, (int16_t)(y + 22));
    _tft->drawString("OTL", 164, (int16_t)(y + 22));
    _tft->drawString("L", 204, (int16_t)(y + 22));
    _tft->drawString("PTS", 230, (int16_t)(y + 22));

    const uint8_t maxRows = (uint8_t)((secH - 28) / 12);
    const uint8_t rowsToDraw = (group.rowCount < maxRows) ? group.rowCount : maxRows;

    for (uint8_t ri = 0; ri < rowsToDraw; ++ri) {
      const StandingsRow &row = group.rows[ri];
      const int16_t ry = (int16_t)(y + 34 + ri * 12);
      const bool isCanada = (row.abbr == focusTeamAbbr);
      if (isCanada) {
        _tft->fillRect(6, (int16_t)(ry - 5), w - 12, 11, Palette::PANEL_2);
      }

      _tft->setTextColor(isCanada ? Palette::WHITE : Palette::GREY, isCanada ? Palette::PANEL_2 : Palette::PANEL);
      _tft->setTextFont(1);
      _tft->setTextDatum(ML_DATUM);
      _tft->drawString(row.abbr, 10, ry);
      _tft->setTextDatum(MC_DATUM);
      _tft->drawString(String(row.w), 92, ry);
      _tft->drawString(String(row.otw), 128, ry);
      _tft->drawString(String(row.otl), 168, ry);
      _tft->drawString(String(row.l), 204, ry);
      _tft->drawString(String(row.pts), 232, ry);
    }
  }

  if (g.standings.usedRegulationFallback) {
    _tft->setTextDatum(MR_DATUM);
    _tft->setTextColor(Palette::GREY, Palette::BG);
    _tft->setTextFont(1);
    _tft->drawString("* OT/SO inferred fallback", (int16_t)(w - 4), (int16_t)(h - 4));
  }
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static void drawScoreboardRow(TFT_eSPI &tft,
                              const TeamLine &away,
                              const TeamLine &home,
                              int16_t panelX,
                              int16_t panelW,
                              int16_t rowTop,
                              int16_t logoSize,
                              bool showAbbr,
                              bool showScores,
                              const String &midLabel) {
  const int16_t padding = (panelW >= 300) ? 6 : 5;
  const int16_t logoY = rowTop;
  const int16_t logoYMid = (int16_t)(logoY + logoSize / 2);

  const int16_t leftLogoX = (int16_t)(panelX + padding);
  const int16_t rightLogoX = (int16_t)(panelX + panelW - padding - logoSize);

  const int16_t scoreAreaX = (int16_t)(leftLogoX + logoSize + padding);
  const int16_t scoreAreaW = (int16_t)(rightLogoX - padding - scoreAreaX);

  const int16_t leftScoreX = (int16_t)(scoreAreaX + scoreAreaW / 4);
  const int16_t dashX = (int16_t)(scoreAreaX + scoreAreaW / 2);
  const int16_t rightScoreX = (int16_t)(scoreAreaX + (scoreAreaW * 3) / 4);
  const int16_t scoreY = (int16_t)(logoYMid + 2);

  const bool bigScores = (scoreAreaW >= 120);
  const int16_t scoreFont = bigScores ? 6 : 4;
  const int16_t scoreBoxW = bigScores ? 56 : 44;
  const int16_t scoreBoxH = bigScores ? 36 : 28;

  tft.fillRect(leftLogoX, logoY, logoSize, logoSize, Palette::BG);
  tft.fillRect(rightLogoX, logoY, logoSize, logoSize, Palette::BG);
  if (showScores) {
    tft.fillRect((int16_t)(leftScoreX - scoreBoxW / 2), (int16_t)(scoreY - scoreBoxH / 2), scoreBoxW, scoreBoxH, Palette::PANEL);
    tft.fillRect((int16_t)(rightScoreX - scoreBoxW / 2), (int16_t)(scoreY - scoreBoxH / 2), scoreBoxW, scoreBoxH, Palette::PANEL);
  }

  const bool canShowAbbr = showAbbr && (logoSize <= 72);
  if (canShowAbbr) {
    const int16_t abbrY = (int16_t)(logoY + logoSize + 12);
    tft.fillRect((int16_t)(leftLogoX - 2), (int16_t)(abbrY - 10), (int16_t)(logoSize + 4), 20, Palette::BG);
    tft.fillRect((int16_t)(rightLogoX - 2), (int16_t)(abbrY - 10), (int16_t)(logoSize + 4), 20, Palette::BG);
  }

  Assets::drawLogo(tft, away.abbr, away.logoUrl, leftLogoX, logoY, logoSize);
  Assets::drawLogo(tft, home.abbr, home.logoUrl, rightLogoX, logoY, logoSize);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(Palette::WHITE, Palette::PANEL);

  if (showScores) {
    tft.setTextFont(scoreFont);
    tft.drawString(String(away.score), leftScoreX, scoreY);
    tft.drawString(String(home.score), rightScoreX, scoreY);
  }

  String mid = midLabel;
  if (mid.isEmpty() && showScores) mid = "-";
  if (mid.length()) {
    tft.setTextFont(bigScores ? 4 : 2);
    tft.drawString(mid, dashX, scoreY);
  }

  if (canShowAbbr) {
    const int16_t abbrY = (int16_t)(logoY + logoSize + 12);
    tft.setTextFont(2);
    tft.drawString(away.abbr, (int16_t)(leftLogoX + logoSize / 2), abbrY);
    tft.drawString(home.abbr, (int16_t)(rightLogoX + logoSize / 2), abbrY);
  }
}



















