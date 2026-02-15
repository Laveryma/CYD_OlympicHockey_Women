#pragma once
#include <TFT_eSPI.h>
#include "types.h"

class Ui {
public:
  void begin(TFT_eSPI &tft, uint8_t rotation);
  void setRotation(uint8_t rotation);
  void setBacklight(uint8_t pct);
  void drawBootSplash(const String &line1, const String &line2);

  void drawNextGame(const GameState &g, const String &focusTeamAbbr);
  void drawLive(const GameState &g);
  void drawGoal(const GameState &g);
  void drawFinal(const GameState &g);
  void drawIntermission(const GameState &g);
  void drawPregame(const GameState &g, const String &focusTeamAbbr);  // legacy wrapper
  void drawLastGame(const GameState &g);
  void drawNoGame(const GameState &g, const String &focusTeamAbbr);    // legacy wrapper
  void drawStandings(const GameState &g, const String &focusTeamAbbr);

private:
  struct ScoreCache {
    bool valid = false;
    String homeAbbr;
    String awayAbbr;
    int homeScore = 0;
    int awayScore = 0;
  };

  struct StatsCache {
    bool valid = false;
    int homeSog = -1;
    int awaySog = -1;
    int homeHits = -1;
    int awayHits = -1;
    int homeFo = -1;
    int awayFo = -1;
  };

  struct StatusCache {
    bool valid = false;
    String left;
    String right;
    uint16_t dotCol = 0;
    bool showDot = false;
  };

  struct InfoCache {
    bool valid = false;
    String line1;
    String line2;
  };

  TFT_eSPI *_tft = nullptr;
  uint8_t _rotation = 0;
  ScreenMode _lastMode = ScreenMode::NEXT_GAME;
  bool _hasLastMode = false;
  String _noGameKey;
  String _preKey;
  String _lastGameKey;
  String _countdownKey;
  String _countdownValue;
  String _countdownDate;
  String _countdownLocation;
  String _standingsKey;

  ScoreCache _liveScore;
  StatsCache _liveStats;
  StatusCache _liveStatus;

  ScoreCache _interScore;
  StatsCache _interStats;
  StatusCache _interStatus;

  ScoreCache _finalScore;
  StatsCache _finalStats;
  StatusCache _finalStatus;

  ScoreCache _preScore;
  StatusCache _preStatus;
  InfoCache _preInfo;

  bool ensureScreen(ScreenMode mode);
  void resetCaches();
  void drawFrame();
  void framePanel(int16_t x, int16_t y, int16_t w, int16_t h);
  void drawTopScorePanel(const GameState &g, const String &label, bool showScores, const String &midLabel);
  void drawStatsBand(const GameState &g);
  void drawStatusBar(const String &left, const String &right, uint16_t dotCol, bool showDot);
};
