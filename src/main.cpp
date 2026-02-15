#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "ui.h"
#include "espn_olympic_client.h"
#include "types.h"
#include "assets.h"
#include "wifi_fallback.h"
#include "anthem.h"
#include "config.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static TFT_eSPI tft;
static Ui ui;
static EspnOlympicClient olympic;
static GameState g;
static ScreenMode mode = ScreenMode::NEXT_GAME;
static bool manualOverride = false;
static uint8_t manualIndex = 0;
static const ScreenMode kManualScreens[] = {
  ScreenMode::LAST_GAME,  ScreenMode::NEXT_GAME,  ScreenMode::LIVE,  ScreenMode::INTERMISSION,  ScreenMode::FINAL,  ScreenMode::GOAL,  ScreenMode::STANDINGS}
;
static const uint8_t kManualScreenCount = sizeof(kManualScreens) / sizeof(kManualScreens[0]);
static bool bootBtnLastRead = true;
static bool bootBtnStable = true;
static uint32_t bootBtnLastChange = 0;
static uint32_t bootBtnPressedAt = 0;
static bool bootBtnLongPressHandled = false;
static const uint32_t kBootBtnLongPressMs = 1400;
static uint32_t lastScoreboardPoll = 0;
static uint32_t lastDetailPoll = 0;
static uint32_t goalBannerUntil = 0;
static uint32_t lastSeenGoalEvent = 0;
static uint32_t lastGoodFetchMs = 0;
static bool lastStaleShown = true;
static bool lastWifiShown = false;
static const uint32_t DATA_STALE_MS = 60000;
static bool timeConfigured = false;
static uint32_t lastTimeConfigAttempt = 0;
static uint32_t lastNextGameRedraw = 0;
struct GoalEvent {
  uint32_t eventId = 0;
  String goalText;
  String goalTeamAbbr;
  String goalTeamLogoUrl;
  String goalScorer;
  bool focusJustScored = false;
}
;
static const uint8_t kGoalQueueSize = 4;
static GoalEvent goalQueue[kGoalQueueSize];
static uint8_t goalHead = 0;
static uint8_t goalTail = 0;
static uint8_t goalCount = 0;
static void ensureTimeConfigured(uint32_t now) {
  if (timeConfigured) return;
  if (now - lastTimeConfigAttempt < 15000) return;
  lastTimeConfigAttempt = now;
  setenv("TZ", TZ_INFO, 1);
  tzset();
  configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);
  timeConfigured = (time(nullptr) > 1577836800);
}
static const char *modeName(ScreenMode m) {
  switch (m) {
    case ScreenMode::NEXT_GAME: return "NEXT_GAME";
    case ScreenMode::LIVE: return "LIVE";
    case ScreenMode::INTERMISSION: return "INTERMISSION";
    case ScreenMode::FINAL: return "FINAL";
    case ScreenMode::LAST_GAME: return "LAST_GAME";
    case ScreenMode::GOAL: return "GOAL";
    case ScreenMode::STANDINGS: return "STANDINGS";
    case ScreenMode::PRE_GAME: return "PRE_GAME";
    case ScreenMode::NO_GAME: return "NO_GAME";
    default: return "UNKNOWN";
  }
}
static void logModeChange(ScreenMode from, ScreenMode to, const char *reason) {
  if (from == to) return;
  if (reason && reason[0]) {
    Serial.printf("STATE: %s -> %s (%s)\n", modeName(from), modeName(to), reason);
  }
  else {
    Serial.printf("STATE: %s -> %s\n", modeName(from), modeName(to));
  }
}
static void refreshMeta(uint32_t now) {
  g.wifiConnected = (WiFi.status() == WL_CONNECTED);
  g.dataStale = (lastGoodFetchMs == 0) || (now - lastGoodFetchMs > DATA_STALE_MS);
  g.lastGoodFetchMs = lastGoodFetchMs;
}
static ScreenMode computeMode(const GameState &st) {
  if (!st.hasGame) {
    if (st.hasNextGame) return ScreenMode::NEXT_GAME;
    if (st.last.hasGame) return ScreenMode::LAST_GAME;
    return ScreenMode::NEXT_GAME;
  }
  if (st.isIntermission) return ScreenMode::INTERMISSION;
  if (st.isLive) return ScreenMode::LIVE;
  if (st.isPre) return ScreenMode::NEXT_GAME;
  if (st.isFinal) return ScreenMode::FINAL;
  return ScreenMode::NEXT_GAME;
}
static void render(ScreenMode m, const GameState &st) {
  switch (m) {
    case ScreenMode::NEXT_GAME:    ui.drawNextGame(st, FOCUS_TEAM_ABBR);
    break;
    case ScreenMode::LIVE:         ui.drawLive(st);
    break;
    case ScreenMode::INTERMISSION: ui.drawIntermission(st);
    break;
    case ScreenMode::FINAL:        ui.drawFinal(st);
    break;
    case ScreenMode::LAST_GAME:    ui.drawLastGame(st);
    break;
    case ScreenMode::GOAL:         ui.drawGoal(st);
    break;
    case ScreenMode::STANDINGS:    ui.drawStandings(st, FOCUS_TEAM_ABBR);
    break;
    case ScreenMode::PRE_GAME:     ui.drawNextGame(st, FOCUS_TEAM_ABBR);
    break;
    case ScreenMode::NO_GAME:      ui.drawNextGame(st, FOCUS_TEAM_ABBR);
    break;
  }
}
static void applyManualScreen() {
  if (manualOverride) {
    ScreenMode target = kManualScreens[manualIndex];
    logModeChange(mode, target, "manual");
    mode = target;
    render(mode, g);
  }
  else {
    ScreenMode nextMode = computeMode(g);
    logModeChange(mode, nextMode, "auto");
    mode = nextMode;
    render(mode, g);
  }
}
static bool goalQueueContains(uint32_t eventId) {
  for (uint8_t i = 0;
  i < goalCount;
  ++i) {
    uint8_t idx = (uint8_t)((goalHead + i) % kGoalQueueSize);
    if (goalQueue[idx].eventId == eventId) return true;
  }
  return false;
}
static void enqueueGoalEvent(const GoalEvent &ev) {
  if (ev.eventId == 0) return;
  if (goalQueueContains(ev.eventId)) return;
  if (goalCount >= kGoalQueueSize) {
    goalHead = (uint8_t)((goalHead + 1) % kGoalQueueSize);
    goalCount--;
  }
  goalQueue[goalTail] = ev;
  goalTail = (uint8_t)((goalTail + 1) % kGoalQueueSize);
  goalCount++;
}
static bool dequeueGoalEvent(GoalEvent &out) {
  if (goalCount == 0) return false;
  out = goalQueue[goalHead];
  goalHead = (uint8_t)((goalHead + 1) % kGoalQueueSize);
  goalCount--;
  return true;
}
static void showGoalEvent(const GoalEvent &ev, uint32_t now) {
  g.goalText = ev.goalText;
  g.goalTeamAbbr = ev.goalTeamAbbr;
  g.goalTeamLogoUrl = ev.goalTeamLogoUrl;
  g.goalScorer = ev.goalScorer;
  g.focusJustScored = ev.focusJustScored;
  g.lastGoalEventId = ev.eventId;
  logModeChange(mode, ScreenMode::GOAL, "goal");
  mode = ScreenMode::GOAL;
  render(mode, g);
  goalBannerUntil = now + 9000;
}
static void maybeShowQueuedGoal(uint32_t now) {
  if (manualOverride || goalBannerUntil > now || mode == ScreenMode::GOAL) return;
  GoalEvent ev;
  if (dequeueGoalEvent(ev)) {
    showGoalEvent(ev, now);
  }
}
static void handleBootButton(uint32_t now) {
  const bool read = (digitalRead(BOOT_BTN_PIN) == HIGH);
  if (read != bootBtnLastRead) {
    bootBtnLastRead = read;
    bootBtnLastChange = now;
  }
  if ((now - bootBtnLastChange) < 40) return;
  if (read != bootBtnStable) {
    bootBtnStable = read;
    if (!bootBtnStable) {
      bootBtnPressedAt = now;
      bootBtnLongPressHandled = false;
      if (!manualOverride) {
        manualOverride = true;
        manualIndex = 0;
      }
      else {
        manualIndex++;
        if (manualIndex >= kManualScreenCount) {
          manualOverride = false;
          manualIndex = 0;
        }
      }
      applyManualScreen();
    } else {
      bootBtnPressedAt = 0;
      bootBtnLongPressHandled = false;
    }
  }

  if (!bootBtnStable &&
      !bootBtnLongPressHandled &&
      bootBtnPressedAt > 0 &&
      (now - bootBtnPressedAt >= kBootBtnLongPressMs)) {
    bootBtnLongPressHandled = true;
    Serial.println("BOOT: long press -> anthem test");
    Anthem::playNow();
  }
}
void setup() {
  Serial.begin(115200);
  ledcSetup(CYD_BL_PWM_CH, 5000, 8);
  ledcAttachPin(TFT_BL, CYD_BL_PWM_CH);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  bootBtnLastRead = (digitalRead(BOOT_BTN_PIN) == HIGH);
  bootBtnStable = bootBtnLastRead;
  bootBtnLastChange = millis();
  uint8_t rotation = TFT_ROTATION;
  ui.begin(tft, rotation);
  if (tft.width() < tft.height()) {
    rotation = (rotation == 1) ? 3 : 1;
    ui.setRotation(rotation);
  }
  ui.setBacklight(85);
  Assets::begin(tft);
  Anthem::begin();
  ui.drawBootSplash("MILANO CORTINA 2026", "WOMEN'S ICE HOCKEY - CONNECTING WIFI");
  wifiConnectWithFallback();
  const uint32_t now = millis();
  ensureTimeConfigured(now);
  lastScoreboardPoll = now - POLL_SCOREBOARD_MS;
  lastDetailPoll = now - POLL_GAMEDETAIL_MS;
  refreshMeta(now);
  render(ScreenMode::NEXT_GAME, g);
  Anthem::prime(g);
}
void loop() {
  wifiTick();
  const uint32_t now = millis();
  handleBootButton(now);
  if (WiFi.status() == WL_CONNECTED) {
    ensureTimeConfigured(now);
  }
  refreshMeta(now);
  if (g.dataStale != lastStaleShown || g.wifiConnected != lastWifiShown) {
    lastStaleShown = g.dataStale;
    lastWifiShown = g.wifiConnected;
    if (!(mode == ScreenMode::GOAL && goalBannerUntil > now)) {
      applyManualScreen();
    }
  }
  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected && now - lastScoreboardPoll >= POLL_SCOREBOARD_MS) {
    lastScoreboardPoll = now;
    GameState next;
    if (olympic.fetchScoreboardNow(next, FOCUS_TEAM_ABBR)) {
      const String prevGameId = g.gameId;
      const String prevHomeAbbr = g.home.abbr;
      const String prevAwayAbbr = g.away.abbr;
      const TeamLine prevHome = g.home;
      const TeamLine prevAway = g.away;
      if (!prevGameId.isEmpty() &&          next.gameId == prevGameId &&          next.home.abbr == prevHomeAbbr &&          next.away.abbr == prevAwayAbbr) {
        if (next.home.sog < 0) next.home.sog = prevHome.sog;
        if (next.home.hits < 0) next.home.hits = prevHome.hits;
        if (next.home.foPct < 0) next.home.foPct = prevHome.foPct;
        if (next.away.sog < 0) next.away.sog = prevAway.sog;
        if (next.away.hits < 0) next.away.hits = prevAway.hits;
        if (next.away.foPct < 0) next.away.foPct = prevAway.foPct;
      }
      g = next;
      lastGoodFetchMs = now;
      refreshMeta(now);
      Anthem::tick(g);
      if (goalBannerUntil <= now && !manualOverride) {
        ScreenMode nextMode = computeMode(g);
        logModeChange(mode, nextMode, "scoreboard");
        mode = nextMode;
        render(mode, g);
      }
    }
    else {
      Serial.println("Scoreboard fetch failed");
    }
  }
  if (wifiConnected && g.hasGame && !g.isFinal && !g.isPre && (now - lastDetailPoll >= POLL_GAMEDETAIL_MS)) {
    lastDetailPoll = now;
    olympic.fetchGameSummaryStats(g);
    GameState tmp = g;
    const bool gotGoal = olympic.fetchLatestGoal(tmp, FOCUS_TEAM_ABBR);
    if (tmp.home.foPct >= 0) g.home.foPct = tmp.home.foPct;
    if (tmp.away.foPct >= 0) g.away.foPct = tmp.away.foPct;
    if (tmp.home.sog >= 0) g.home.sog = tmp.home.sog;
    if (tmp.away.sog >= 0) g.away.sog = tmp.away.sog;
    if (tmp.home.hits >= 0) g.home.hits = tmp.home.hits;
    if (tmp.away.hits >= 0) g.away.hits = tmp.away.hits;
    if (tmp.strengthLabel.length()) g.strengthLabel = tmp.strengthLabel;
    if (gotGoal && tmp.lastGoalEventId != 0 && tmp.lastGoalEventId != lastSeenGoalEvent) {
      lastSeenGoalEvent = tmp.lastGoalEventId;
      GoalEvent ev;
      ev.eventId = tmp.lastGoalEventId;
      ev.goalText = tmp.goalText;
      ev.goalTeamAbbr = tmp.goalTeamAbbr;
      ev.goalTeamLogoUrl = tmp.goalTeamLogoUrl;
      ev.goalScorer = tmp.goalScorer;
      ev.focusJustScored = tmp.focusJustScored;
      enqueueGoalEvent(ev);
    }
  }
  if (!manualOverride) {
    maybeShowQueuedGoal(now);
  }
  if (mode == ScreenMode::NEXT_GAME && (g.hasNextGame || g.isPre) && now - lastNextGameRedraw >= 1000) {
    lastNextGameRedraw = now;
    ui.drawNextGame(g, FOCUS_TEAM_ABBR);
  }
  if (!manualOverride && goalBannerUntil > 0 && goalBannerUntil <= now && mode == ScreenMode::GOAL) {
    goalBannerUntil = 0;
    GoalEvent ev;
    if (dequeueGoalEvent(ev)) {
      showGoalEvent(ev, now);
    }
    else {
      ScreenMode nextMode = computeMode(g);
      logModeChange(mode, nextMode, "goal-timeout");
      mode = nextMode;
      render(mode, g);
    }
  }
  delay(10);
}



