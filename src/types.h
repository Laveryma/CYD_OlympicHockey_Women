#pragma once
#include <Arduino.h>
#include <stdint.h>
#include <time.h>

enum class ScreenMode : uint8_t {
  NEXT_GAME,
  LIVE,
  GOAL,
  INTERMISSION,
  FINAL,
  LAST_GAME,
  STANDINGS,
  PRE_GAME,  // legacy alias, not used in Olympic flow
  NO_GAME    // legacy alias, not used in Olympic flow
};

struct TeamLine {
  String abbr;
  String name;
  String logoUrl;
  int score = 0;
  int sog = -1;
  int hits = -1;
  int foPct = -1;
};

static const uint8_t kRecapMaxScorers = 3;
static const uint8_t kRecapMaxPeriods = 5;

struct ScorerEntry {
  String name;
  uint8_t goals = 0;
};

struct PeriodEntry {
  String label;
  uint8_t home = 0;
  uint8_t away = 0;
};

static const uint8_t kMaxStandingsGroups = 3;
static const uint8_t kMaxStandingsRows = 6;

struct StandingsRow {
  String abbr;
  uint8_t gp = 0;
  uint8_t w = 0;
  uint8_t otw = 0;
  uint8_t otl = 0;
  uint8_t l = 0;
  uint8_t pts = 0;
  int16_t gf = 0;
  int16_t ga = 0;
};

struct GroupStandings {
  char group = '?';
  uint8_t rowCount = 0;
  StandingsRow rows[kMaxStandingsRows];
};

struct OlympicStandings {
  uint8_t groupCount = 0;
  GroupStandings groups[kMaxStandingsGroups];
  char canadaGroup = '?';
  int8_t canadaRank = -1;  // 1-based rank within group, -1 when unknown
  uint8_t canadaPts = 0;
  bool usedRegulationFallback = false;
};

struct LastGameRecap {
  bool hasGame = false;
  String gameId;
  TeamLine away;
  TeamLine home;
  time_t startEpoch = 0;
  String venue;
  String city;
  uint8_t awayScorerCount = 0;
  ScorerEntry awayScorers[kRecapMaxScorers];
  uint8_t homeScorerCount = 0;
  ScorerEntry homeScorers[kRecapMaxScorers];
  uint8_t periodCount = 0;
  PeriodEntry periods[kRecapMaxPeriods];
};

struct GameState {
  bool hasGame = false;
  bool isFinal = false;
  bool isIntermission = false;
  bool isLive = false;
  bool isPre = false;

  String gameId;
  String startTimeHHMM;
  time_t startEpoch = 0;
  String statusDetail;
  String statusShortDetail;
  String clock;
  int period = 0;
  String groupHeadline;
  char group = '?';

  String strengthLabel;
  uint16_t strengthColour = 0;

  TeamLine away;
  TeamLine home;

  uint32_t lastGoalEventId = 0;
  bool focusJustScored = false;
  String goalTeamAbbr;
  String goalTeamLogoUrl;
  String goalScorer;
  String goalText;

  // Next game fallback.
  bool hasNextGame = false;
  String nextOppAbbr;
  String nextOppLogoUrl;
  String nextFocusLogoUrl;
  bool nextIsHome = false;
  String nextVenue;
  String nextCity;
  char nextGroup = '?';
  String nextGroupHeadline;
  time_t nextStartEpoch = 0;

  // Data freshness / connectivity (set by main loop).
  bool dataStale = false;
  bool wifiConnected = false;
  uint32_t lastGoodFetchMs = 0;

  // Last game recap.
  LastGameRecap last;

  // Group standings for Olympic tournament.
  OlympicStandings standings;
};

