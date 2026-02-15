#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "types.h"

class EspnOlympicClient {
public:
  // Tournament feed fetch + selection (in-progress > next scheduled > most recent final).
  bool fetchScoreboardNow(GameState &out, const String &focusTeamAbbr);
  bool fetchScoreboardForRange(GameState &out,
                               const String &focusTeamAbbr,
                               const String &startYYYYMMDD,
                               const String &endYYYYMMDD);

  // Convenience wrappers for explicit next/last selection from the same endpoint.
  bool fetchNextCanadaGame(GameState &io, const String &focusTeamAbbr);
  bool fetchLastCanadaGame(GameState &io, const String &focusTeamAbbr);

  // Optional detail endpoint for stats/plays. App still runs if these fail.
  bool fetchGameSummaryStats(GameState &io);
  bool fetchLatestGoal(GameState &io, const String &focusTeamAbbr);

private:
  bool httpGetJson(const String &url, JsonDocument &doc);
  bool httpGetJson(const String &url, JsonDocument &doc, const JsonDocument &filter);
};
