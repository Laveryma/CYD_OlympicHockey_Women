#include "espn_olympic_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {

static const char *kEspnBase = "https://site.api.espn.com/apis/site/v2/sports/hockey/olympics-womens-ice-hockey";
static const char *kTournamentStart = "20260101";
static const char *kTournamentEnd = "20260222";

static const uint8_t kMaxParsedEvents = 80;

struct ParsedEvent {
  bool valid = false;
  String id;
  time_t startEpoch = 0;
  String state;
  bool completed = false;
  String detail;
  String shortDetail;
  String displayClock;
  int period = 0;
  String groupHeadline;
  char group = '?';
  bool preliminaryRound = false;
  TeamLine home;
  TeamLine away;
  String venue;
  String city;
  bool hasCanada = false;
  bool isOvertime = false;
  bool hasOtIndicator = false;
};

// Keep parsed-event storage out of the loop task stack to avoid watchdog resets
// when handling ESPN's larger payloads.
static ParsedEvent g_parsedEvents[kMaxParsedEvents];

// ESP32 Arduino toolchains differ: some expose `timegm()`, some don't.
// We only need a UTC ISO-8601 timestamp -> Unix epoch seconds converter.
static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= (m <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool parseIsoUtcToEpoch(const String &iso, time_t &outEpoch) {
  int y = 0;
  int mo = 0;
  int d = 0;
  int hh = 0;
  int mm = 0;
  int ss = 0;
  int n = sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &hh, &mm, &ss);
  if (n < 5) return false;
  if (n == 5) ss = 0;

  const int64_t days = daysFromCivil(y, (unsigned)mo, (unsigned)d);
  const int64_t secs = days * 86400LL + (int64_t)hh * 3600LL + (int64_t)mm * 60LL + (int64_t)ss;
  outEpoch = (time_t)secs;
  return true;
}

static bool strContainsIgnoreCase(const String &haystack, const char *needle) {
  if (!needle || !needle[0]) return true;
  String h = haystack;
  String n = needle;
  h.toLowerCase();
  n.toLowerCase();
  return h.indexOf(n) >= 0;
}

static String trimAndUpper(const String &in) {
  String out = in;
  out.trim();
  out.toUpperCase();
  return out;
}

static int parseIntLoose(const String &value) {
  bool hasDigit = false;
  int sign = 1;
  long out = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    if (!hasDigit && c == '-') {
      sign = -1;
      continue;
    }
    if (c >= '0' && c <= '9') {
      out = out * 10 + (c - '0');
      hasDigit = true;
      continue;
    }
    if (hasDigit) break;
  }
  return hasDigit ? (int)(out * sign) : -1;
}

static int parsePercentLoose(const String &value) {
  const int v = parseIntLoose(value);
  if (v < 0) return -1;
  if (v > 100) return 100;
  return v;
}

static String hhmmFromEpochLocal(time_t epoch) {
  if (epoch <= 0) return String("");
  struct tm lt;
  localtime_r(&epoch, &lt);
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &lt);
  return String(buf);
}

static char parseGroupLetter(const String &headline) {
  const int idx = headline.indexOf("Group ");
  if (idx < 0) return '?';
  const int letterIdx = idx + 6;
  if (letterIdx >= (int)headline.length()) return '?';
  char g = headline.charAt(letterIdx);
  if (g >= 'a' && g <= 'z') g = (char)(g - 'a' + 'A');
  if (g < 'A' || g > 'Z') return '?';
  return g;
}

static bool detectOvertime(const ParsedEvent &ev, bool &hasIndicator) {
  hasIndicator = false;
  const String detail = trimAndUpper(ev.detail);
  const String shortDetail = trimAndUpper(ev.shortDetail);

  if (detail.indexOf("/OT") >= 0 || detail.indexOf(" OT") >= 0 ||
      shortDetail.indexOf("/OT") >= 0 || shortDetail.indexOf(" OT") >= 0) {
    hasIndicator = true;
    return true;
  }
  if (detail.indexOf("/SO") >= 0 || detail.indexOf(" SO") >= 0 ||
      shortDetail.indexOf("/SO") >= 0 || shortDetail.indexOf(" SO") >= 0) {
    hasIndicator = true;
    return true;
  }
  if (ev.period > 3) {
    hasIndicator = true;
    return true;
  }
  if (detail.startsWith("FINAL") || shortDetail.startsWith("FINAL")) {
    hasIndicator = true;
    return false;
  }

  // Fallback requested by spec when OT/SO is not reliably detectable.
  hasIndicator = false;
  return false;
}

static void clearRecap(LastGameRecap &recap) {
  recap = LastGameRecap();
}

class ChunkedStream : public Stream {
public:
  explicit ChunkedStream(Stream &src) : _src(src) {}

  int available() override {
    if (_done) return 0;
    if (_peeked >= 0) return 1;
    if (_remaining > 0) {
      int avail = _src.available();
      if (avail > _remaining) avail = (int)_remaining;
      return avail;
    }
    return _src.available();
  }

  int read() override {
    if (_peeked >= 0) {
      int c = _peeked;
      _peeked = -1;
      return c;
    }
    if (_done) return -1;
    if (_remaining == 0) {
      if (!readChunkHeader()) return -1;
    }
    int c = _src.read();
    if (c < 0) return -1;
    _remaining--;
    if (_remaining == 0) consumeCRLF();
    return c;
  }

  int peek() override {
    if (_peeked < 0) _peeked = read();
    return _peeked;
  }

  void flush() override {}
  size_t write(uint8_t) override { return 0; }

private:
  Stream &_src;
  int _peeked = -1;
  int32_t _remaining = 0;
  bool _done = false;

  bool readChunkHeader() {
    char line[24];
    size_t n = _src.readBytesUntil('\n', line, sizeof(line) - 1);
    if (n == 0) return false;
    line[n] = '\0';
    if (n && line[n - 1] == '\r') line[n - 1] = '\0';
    char *semi = strchr(line, ';');
    if (semi) *semi = '\0';
    _remaining = (int32_t)strtol(line, nullptr, 16);
    if (_remaining == 0) {
      while (true) {
        n = _src.readBytesUntil('\n', line, sizeof(line) - 1);
        if (n == 0) break;
        line[n] = '\0';
        if (n == 1 && line[0] == '\r') break;
        if (line[0] == '\r') break;
      }
      _done = true;
      return false;
    }
    return true;
  }

  void consumeCRLF() {
    (void)_src.read();
    (void)_src.read();
  }
};

static bool httpGetJsonInternal(const String &url, JsonDocument &doc, const JsonDocument *filter) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12000);

  HTTPClient http;
  const uint32_t timeoutMs = 12000;
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  const char *headers[] = {"Location", "Content-Type", "Content-Length", "Transfer-Encoding"};
  http.collectHeaders(headers, 4);

  Serial.printf("HTTP GET: %s\n", url.c_str());

  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "olympic-scoreboard-esp32");
  http.addHeader("Accept", "application/json");

  const uint32_t started = millis();
  const int code = http.GET();
  const uint32_t elapsed = millis() - started;
  if (code <= 0) {
    Serial.printf("HTTP error: %s (%d) after %lums\n", http.errorToString(code).c_str(), code, (unsigned long)elapsed);
    http.end();
    return false;
  }

  Serial.printf("HTTP status: %d in %lums\n", code, (unsigned long)elapsed);
  if (code != 200) {
    String location = http.header("Location");
    if (location.length()) Serial.printf("Location: %s\n", location.c_str());
    String body = http.getString();
    if (body.length()) {
      Serial.printf("Body (first 200): %s\n", body.substring(0, 200).c_str());
    }
    http.end();
    return false;
  }

  const String transferEncoding = http.header("Transfer-Encoding");
  Stream &stream = http.getStream();
  const auto nesting = DeserializationOption::NestingLimit(24);
  DeserializationError err;
  if (transferEncoding.equalsIgnoreCase("chunked")) {
    ChunkedStream chunked(stream);
    err = filter ? deserializeJson(doc, chunked, DeserializationOption::Filter(*filter), nesting)
                 : deserializeJson(doc, chunked, nesting);
  } else {
    err = filter ? deserializeJson(doc, stream, DeserializationOption::Filter(*filter), nesting)
                 : deserializeJson(doc, stream, nesting);
  }

  http.end();
  if (err) {
    Serial.printf("JSON parse failed: %s\n", err.c_str());
  }
  return !err;
}

static int findGroupIndex(const OlympicStandings &standings, char group) {
  for (uint8_t i = 0; i < standings.groupCount; ++i) {
    if (standings.groups[i].group == group) return i;
  }
  return -1;
}

struct StandingAcc {
  bool used = false;
  char group = '?';
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

static StandingAcc *findOrCreateAcc(StandingAcc *acc,
                                    uint8_t maxAcc,
                                    uint8_t &count,
                                    char group,
                                    const String &abbr) {
  for (uint8_t i = 0; i < count; ++i) {
    if (acc[i].used && acc[i].group == group && acc[i].abbr == abbr) return &acc[i];
  }
  if (count >= maxAcc) return nullptr;
  acc[count].used = true;
  acc[count].group = group;
  acc[count].abbr = abbr;
  return &acc[count++];
}

static bool standingsRowBetter(const StandingsRow &a, const StandingsRow &b) {
  if (a.pts != b.pts) return a.pts > b.pts;
  const int diffA = a.gf - a.ga;
  const int diffB = b.gf - b.ga;
  if (diffA != diffB) return diffA > diffB;
  if (a.gf != b.gf) return a.gf > b.gf;
  return a.abbr < b.abbr;
}

static void sortGroupRows(GroupStandings &group) {
  for (uint8_t i = 0; i < group.rowCount; ++i) {
    for (uint8_t j = i + 1; j < group.rowCount; ++j) {
      if (standingsRowBetter(group.rows[j], group.rows[i])) {
        StandingsRow tmp = group.rows[i];
        group.rows[i] = group.rows[j];
        group.rows[j] = tmp;
      }
    }
  }
}

static bool parseParsedEvents(JsonDocument &doc,
                              ParsedEvent *events,
                              uint8_t maxEvents,
                              uint8_t &eventCount,
                              const String &focusTeamAbbr) {
  eventCount = 0;
  JsonArrayConst all = doc["events"].as<JsonArrayConst>();
  if (all.isNull()) return false;

  for (JsonObjectConst ev : all) {
    if (eventCount >= maxEvents) break;

    JsonArrayConst competitions = ev["competitions"].as<JsonArrayConst>();
    if (competitions.isNull() || competitions.size() == 0) continue;
    JsonObjectConst comp = competitions[0];

    ParsedEvent parsed;
    parsed.valid = true;
    parsed.id = String((const char *)(ev["id"] | ""));

    const char *date_c = ev["date"] | "";
    parseIsoUtcToEpoch(String(date_c ? date_c : ""), parsed.startEpoch);

    const char *state_c = comp["status"]["type"]["state"] | "";
    parsed.state = String(state_c ? state_c : "");
    parsed.completed = comp["status"]["type"]["completed"] | false;

    const char *detail_c = comp["status"]["type"]["detail"] | "";
    const char *shortDetail_c = comp["status"]["type"]["shortDetail"] | "";
    const char *clock_c = comp["status"]["displayClock"] | "";
    parsed.detail = String(detail_c ? detail_c : "");
    parsed.shortDetail = String(shortDetail_c ? shortDetail_c : "");
    parsed.displayClock = String(clock_c ? clock_c : "");
    parsed.period = comp["status"]["period"] | 0;

    const char *headline_c = comp["notes"][0]["headline"] | "";
    parsed.groupHeadline = String(headline_c ? headline_c : "");
    parsed.group = parseGroupLetter(parsed.groupHeadline);
    parsed.preliminaryRound = strContainsIgnoreCase(parsed.groupHeadline, "preliminary round");

    const char *venue_c = comp["venue"]["fullName"] | "";
    const char *city_c = comp["venue"]["address"]["city"] | "";
    parsed.venue = String(venue_c ? venue_c : "");
    parsed.city = String(city_c ? city_c : "");

    JsonArrayConst competitors = comp["competitors"].as<JsonArrayConst>();
    for (JsonObjectConst c : competitors) {
      TeamLine team;
      const char *abbr_c = c["team"]["abbreviation"] | "";
      const char *name_c = c["team"]["displayName"] | "";
      const char *logo_c = c["team"]["logo"] | "";
      const char *score_c = c["score"] | "0";
      const char *homeAway_c = c["homeAway"] | "";

      team.abbr = String(abbr_c ? abbr_c : "");
      team.name = String(name_c ? name_c : "");
      team.logoUrl = String(logo_c ? logo_c : "");
      team.score = parseIntLoose(String(score_c ? score_c : "0"));
      if (team.score < 0) team.score = 0;

      String side = String(homeAway_c ? homeAway_c : "");
      side.toLowerCase();
      if (side == "home") {
        parsed.home = team;
      } else if (side == "away") {
        parsed.away = team;
      } else if (parsed.away.abbr.isEmpty()) {
        parsed.away = team;
      } else {
        parsed.home = team;
      }

      if (team.abbr == focusTeamAbbr) {
        parsed.hasCanada = true;
      }
    }

    bool hasOtIndicator = false;
    parsed.isOvertime = detectOvertime(parsed, hasOtIndicator);
    parsed.hasOtIndicator = hasOtIndicator;

    events[eventCount++] = parsed;
  }

  return true;
}

static void applyEventToState(const ParsedEvent &ev, GameState &out) {
  out.hasGame = true;
  out.gameId = ev.id;
  out.startEpoch = ev.startEpoch;
  out.startTimeHHMM = hhmmFromEpochLocal(ev.startEpoch);
  out.statusDetail = ev.detail;
  out.statusShortDetail = ev.shortDetail;
  out.clock = ev.displayClock;
  out.period = ev.period;
  out.groupHeadline = ev.groupHeadline;
  out.group = ev.group;

  out.home = ev.home;
  out.away = ev.away;

  out.isPre = (ev.state == "pre");
  out.isLive = (ev.state == "in");
  out.isFinal = (ev.state == "post") || ev.completed;
  out.isIntermission = false;
  if (out.isLive) {
    const bool detailIntermission = strContainsIgnoreCase(ev.detail, "intermission") ||
                                    strContainsIgnoreCase(ev.detail, "end of");
    const bool clockIntermission = ((ev.displayClock == "0:00") || (ev.displayClock == "00:00")) && ev.period > 0;
    out.isIntermission = detailIntermission || clockIntermission;
  }

  out.strengthLabel = "EVEN STRENGTH";
  out.strengthColour = 0x07E0;
}

static void populateNextGame(const ParsedEvent *events,
                             uint8_t eventCount,
                             const String &focusTeamAbbr,
                             GameState &out) {
  const time_t nowEpoch = time(nullptr);
  int bestIdx = -1;

  for (uint8_t i = 0; i < eventCount; ++i) {
    const ParsedEvent &ev = events[i];
    if (!ev.hasCanada) continue;
    if (ev.state != "pre") continue;

    if (bestIdx < 0) {
      bestIdx = i;
      continue;
    }

    const time_t cur = events[bestIdx].startEpoch;
    const bool curPast = (cur > 0 && nowEpoch > 1577836800 && cur < nowEpoch);
    const bool candPast = (ev.startEpoch > 0 && nowEpoch > 1577836800 && ev.startEpoch < nowEpoch);

    if (curPast != candPast) {
      if (curPast && !candPast) bestIdx = i;
      continue;
    }

    if (ev.startEpoch > 0 && (cur == 0 || ev.startEpoch < cur)) {
      bestIdx = i;
    }
  }

  out.hasNextGame = false;
  out.nextOppAbbr = "";
  out.nextOppLogoUrl = "";
  out.nextFocusLogoUrl = "";
  out.nextIsHome = false;
  out.nextVenue = "";
  out.nextCity = "";
  out.nextStartEpoch = 0;
  out.nextGroup = '?';
  out.nextGroupHeadline = "";

  if (bestIdx < 0) return;

  const ParsedEvent &next = events[bestIdx];
  out.hasNextGame = true;
  out.nextStartEpoch = next.startEpoch;
  out.nextVenue = next.venue;
  out.nextCity = next.city;
  out.nextGroup = next.group;
  out.nextGroupHeadline = next.groupHeadline;

  if (next.home.abbr == focusTeamAbbr) {
    out.nextIsHome = true;
    out.nextOppAbbr = next.away.abbr;
    out.nextOppLogoUrl = next.away.logoUrl;
    out.nextFocusLogoUrl = next.home.logoUrl;
  } else {
    out.nextIsHome = false;
    out.nextOppAbbr = next.home.abbr;
    out.nextOppLogoUrl = next.home.logoUrl;
    out.nextFocusLogoUrl = next.away.logoUrl;
  }
}

static void populateLastGame(const ParsedEvent *events,
                             uint8_t eventCount,
                             const String &focusTeamAbbr,
                             GameState &out) {
  LastGameRecap recap;
  clearRecap(recap);

  int bestIdx = -1;
  for (uint8_t i = 0; i < eventCount; ++i) {
    const ParsedEvent &ev = events[i];
    if (!ev.hasCanada) continue;
    if (!(ev.state == "post" || ev.completed)) continue;

    if (bestIdx < 0 || ev.startEpoch > events[bestIdx].startEpoch) {
      bestIdx = i;
    }
  }

  if (bestIdx >= 0) {
    const ParsedEvent &last = events[bestIdx];
    recap.hasGame = true;
    recap.gameId = last.id;
    recap.startEpoch = last.startEpoch;
    recap.away = last.away;
    recap.home = last.home;
    recap.venue = last.venue;
    recap.city = last.city;

    // Minimal recap requirement: final score + date + opponent.
    recap.periodCount = 0;
    recap.awayScorerCount = 0;
    recap.homeScorerCount = 0;

    (void)focusTeamAbbr;
  }

  out.last = recap;
}

static void buildStandings(const ParsedEvent *events,
                           uint8_t eventCount,
                           const String &focusTeamAbbr,
                           GameState &out) {
  out.standings = OlympicStandings();

  StandingAcc acc[kMaxStandingsGroups * kMaxStandingsRows];
  uint8_t accCount = 0;
  bool usedFallback = false;

  for (uint8_t i = 0; i < eventCount; ++i) {
    const ParsedEvent &ev = events[i];
    if (!ev.preliminaryRound) continue;
    if (ev.group == '?') continue;
    if (!(ev.state == "post" || ev.completed)) continue;
    if (ev.home.abbr.isEmpty() || ev.away.abbr.isEmpty()) continue;

    StandingAcc *home = findOrCreateAcc(acc, (uint8_t)(kMaxStandingsGroups * kMaxStandingsRows), accCount, ev.group, ev.home.abbr);
    StandingAcc *away = findOrCreateAcc(acc, (uint8_t)(kMaxStandingsGroups * kMaxStandingsRows), accCount, ev.group, ev.away.abbr);
    if (!home || !away) continue;

    home->gp++;
    away->gp++;
    home->gf += ev.home.score;
    home->ga += ev.away.score;
    away->gf += ev.away.score;
    away->ga += ev.home.score;

    const bool homeWon = ev.home.score > ev.away.score;
    const bool awayWon = ev.away.score > ev.home.score;
    if (!homeWon && !awayWon) continue;

    const bool hasIndicator = ev.hasOtIndicator;
    const bool overtime = ev.isOvertime;
    if (!hasIndicator) {
      // Requirement fallback: if OT/SO detection cannot be trusted, treat as regulation for points.
      usedFallback = true;
    }

    StandingAcc *winner = homeWon ? home : away;
    StandingAcc *loser = homeWon ? away : home;

    if (overtime) {
      winner->otw++;
      loser->otl++;
      winner->pts = (uint8_t)(winner->pts + 2);
      loser->pts = (uint8_t)(loser->pts + 1);
    } else {
      winner->w++;
      loser->l++;
      winner->pts = (uint8_t)(winner->pts + 3);
    }
  }

  out.standings.usedRegulationFallback = usedFallback;

  for (uint8_t i = 0; i < accCount; ++i) {
    const char group = acc[i].group;
    int groupIdx = findGroupIndex(out.standings, group);
    if (groupIdx < 0) {
      if (out.standings.groupCount >= kMaxStandingsGroups) continue;
      groupIdx = out.standings.groupCount++;
      out.standings.groups[groupIdx] = GroupStandings();
      out.standings.groups[groupIdx].group = group;
    }

    GroupStandings &g = out.standings.groups[groupIdx];
    if (g.rowCount >= kMaxStandingsRows) continue;

    StandingsRow &row = g.rows[g.rowCount++];
    row.abbr = acc[i].abbr;
    row.gp = acc[i].gp;
    row.w = acc[i].w;
    row.otw = acc[i].otw;
    row.otl = acc[i].otl;
    row.l = acc[i].l;
    row.pts = acc[i].pts;
    row.gf = acc[i].gf;
    row.ga = acc[i].ga;
  }

  for (uint8_t i = 0; i < out.standings.groupCount; ++i) {
    sortGroupRows(out.standings.groups[i]);
  }

  out.standings.canadaGroup = '?';
  out.standings.canadaRank = -1;
  out.standings.canadaPts = 0;
  for (uint8_t g = 0; g < out.standings.groupCount; ++g) {
    GroupStandings &group = out.standings.groups[g];
    for (uint8_t r = 0; r < group.rowCount; ++r) {
      if (group.rows[r].abbr == focusTeamAbbr) {
        out.standings.canadaGroup = group.group;
        out.standings.canadaRank = (int8_t)(r + 1);
        out.standings.canadaPts = group.rows[r].pts;
      }
    }
  }
}

static int selectInProgress(const ParsedEvent *events, uint8_t eventCount) {
  int best = -1;
  for (uint8_t i = 0; i < eventCount; ++i) {
    if (!events[i].hasCanada) continue;
    if (events[i].state != "in") continue;
    if (best < 0 || (events[i].startEpoch > 0 && events[i].startEpoch < events[best].startEpoch)) {
      best = i;
    }
  }
  return best;
}

static int selectNextScheduled(const ParsedEvent *events, uint8_t eventCount) {
  const time_t nowEpoch = time(nullptr);
  int bestFuture = -1;
  int bestAny = -1;

  for (uint8_t i = 0; i < eventCount; ++i) {
    if (!events[i].hasCanada) continue;
    if (events[i].state != "pre") continue;

    if (bestAny < 0 || (events[i].startEpoch > 0 && events[i].startEpoch < events[bestAny].startEpoch)) {
      bestAny = i;
    }

    if (events[i].startEpoch <= 0 || nowEpoch <= 1577836800 || events[i].startEpoch >= nowEpoch) {
      if (bestFuture < 0 || events[i].startEpoch < events[bestFuture].startEpoch) {
        bestFuture = i;
      }
    }
  }

  return (bestFuture >= 0) ? bestFuture : bestAny;
}

static int selectMostRecentFinal(const ParsedEvent *events, uint8_t eventCount) {
  int best = -1;
  for (uint8_t i = 0; i < eventCount; ++i) {
    if (!events[i].hasCanada) continue;
    if (!(events[i].state == "post" || events[i].completed)) continue;
    if (best < 0 || events[i].startEpoch > events[best].startEpoch) {
      best = i;
    }
  }
  return best;
}

static bool applyStatToTeam(TeamLine &team, const String &key, const String &value) {
  if (team.abbr.isEmpty()) return false;

  String lower = key;
  lower.toLowerCase();

  if (lower.indexOf("shot") >= 0) {
    const int v = parseIntLoose(value);
    if (v >= 0) team.sog = v;
    return true;
  }
  if (lower == "hits" || lower.indexOf("hit") >= 0) {
    const int v = parseIntLoose(value);
    if (v >= 0) team.hits = v;
    return true;
  }
  if (lower.indexOf("faceoff") >= 0 || lower.indexOf("face off") >= 0 || lower.indexOf("fo%") >= 0) {
    const int v = parsePercentLoose(value);
    if (v >= 0) team.foPct = v;
    return true;
  }

  return false;
}

}  // namespace

bool EspnOlympicClient::httpGetJson(const String &url, JsonDocument &doc) {
  return httpGetJsonInternal(url, doc, nullptr);
}

bool EspnOlympicClient::httpGetJson(const String &url, JsonDocument &doc, const JsonDocument &filter) {
  return httpGetJsonInternal(url, doc, &filter);
}

bool EspnOlympicClient::fetchScoreboardNow(GameState &out, const String &focusTeamAbbr) {
  return fetchScoreboardForRange(out, focusTeamAbbr, kTournamentStart, kTournamentEnd);
}

bool EspnOlympicClient::fetchScoreboardForRange(GameState &out,
                                                const String &focusTeamAbbr,
                                                const String &startYYYYMMDD,
                                                const String &endYYYYMMDD) {
  out = GameState();

  JsonDocument filter;
  filter["events"][0]["id"] = true;
  filter["events"][0]["date"] = true;
  filter["events"][0]["competitions"][0]["status"]["type"]["state"] = true;
  filter["events"][0]["competitions"][0]["status"]["type"]["completed"] = true;
  filter["events"][0]["competitions"][0]["status"]["type"]["detail"] = true;
  filter["events"][0]["competitions"][0]["status"]["type"]["shortDetail"] = true;
  filter["events"][0]["competitions"][0]["status"]["displayClock"] = true;
  filter["events"][0]["competitions"][0]["status"]["period"] = true;
  filter["events"][0]["competitions"][0]["notes"][0]["headline"] = true;
  filter["events"][0]["competitions"][0]["venue"]["fullName"] = true;
  filter["events"][0]["competitions"][0]["venue"]["address"]["city"] = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["homeAway"] = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["score"] = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["team"]["displayName"] = true;
  filter["events"][0]["competitions"][0]["competitors"][0]["team"]["logo"] = true;

  JsonDocument doc;
  const String url = String(kEspnBase) + "/scoreboard?dates=" + startYYYYMMDD + "-" + endYYYYMMDD;
  if (!httpGetJson(url, doc, filter)) {
    return false;
  }

  uint8_t eventCount = 0;
  if (!parseParsedEvents(doc, g_parsedEvents, kMaxParsedEvents, eventCount, focusTeamAbbr)) {
    return false;
  }

  buildStandings(g_parsedEvents, eventCount, focusTeamAbbr, out);
  populateNextGame(g_parsedEvents, eventCount, focusTeamAbbr, out);
  populateLastGame(g_parsedEvents, eventCount, focusTeamAbbr, out);

  int selected = selectInProgress(g_parsedEvents, eventCount);
  if (selected < 0) selected = selectNextScheduled(g_parsedEvents, eventCount);
  if (selected < 0) selected = selectMostRecentFinal(g_parsedEvents, eventCount);

  if (selected >= 0) {
    applyEventToState(g_parsedEvents[selected], out);
  } else {
    out.hasGame = false;
    out.isPre = false;
    out.isLive = false;
    out.isIntermission = false;
    out.isFinal = false;
  }

  return true;
}

bool EspnOlympicClient::fetchNextCanadaGame(GameState &io, const String &focusTeamAbbr) {
  GameState next;
  if (!fetchScoreboardNow(next, focusTeamAbbr)) return false;
  io.hasNextGame = next.hasNextGame;
  io.nextOppAbbr = next.nextOppAbbr;
  io.nextOppLogoUrl = next.nextOppLogoUrl;
  io.nextFocusLogoUrl = next.nextFocusLogoUrl;
  io.nextIsHome = next.nextIsHome;
  io.nextVenue = next.nextVenue;
  io.nextCity = next.nextCity;
  io.nextStartEpoch = next.nextStartEpoch;
  io.nextGroup = next.nextGroup;
  io.nextGroupHeadline = next.nextGroupHeadline;
  return true;
}

bool EspnOlympicClient::fetchLastCanadaGame(GameState &io, const String &focusTeamAbbr) {
  GameState next;
  if (!fetchScoreboardNow(next, focusTeamAbbr)) return false;
  io.last = next.last;
  return true;
}

bool EspnOlympicClient::fetchGameSummaryStats(GameState &io) {
  if (io.gameId.isEmpty()) return false;

  JsonDocument filter;
  filter["header"]["competitions"][0]["status"]["displayClock"] = true;
  filter["header"]["competitions"][0]["status"]["period"] = true;
  filter["header"]["competitions"][0]["status"]["type"]["state"] = true;
  filter["header"]["competitions"][0]["status"]["type"]["detail"] = true;
  filter["boxscore"]["teams"][0]["team"]["abbreviation"] = true;
  filter["boxscore"]["teams"][0]["statistics"][0]["name"] = true;
  filter["boxscore"]["teams"][0]["statistics"][0]["displayName"] = true;
  filter["boxscore"]["teams"][0]["statistics"][0]["displayValue"] = true;

  JsonDocument doc;
  const String url = String(kEspnBase) + "/summary?event=" + io.gameId;
  if (!httpGetJson(url, doc, filter)) return false;

  const char *clock_c = doc["header"]["competitions"][0]["status"]["displayClock"] | "";
  io.clock = String(clock_c ? clock_c : io.clock.c_str());
  io.period = doc["header"]["competitions"][0]["status"]["period"] | io.period;

  const char *state_c = doc["header"]["competitions"][0]["status"]["type"]["state"] | "";
  const String state = String(state_c ? state_c : "");
  io.isLive = (state == "in");
  io.isPre = (state == "pre");
  io.isFinal = (state == "post");

  const char *detail_c = doc["header"]["competitions"][0]["status"]["type"]["detail"] | "";
  io.statusDetail = String(detail_c ? detail_c : io.statusDetail.c_str());
  io.isIntermission = io.isLive && (strContainsIgnoreCase(io.statusDetail, "intermission") ||
                                    strContainsIgnoreCase(io.statusDetail, "end of"));

  JsonArrayConst teams = doc["boxscore"]["teams"].as<JsonArrayConst>();
  if (!teams.isNull()) {
    for (JsonObjectConst team : teams) {
      const String abbr = String((const char *)(team["team"]["abbreviation"] | ""));
      TeamLine *line = nullptr;
      if (abbr == io.home.abbr) line = &io.home;
      if (abbr == io.away.abbr) line = &io.away;
      if (!line) continue;

      JsonArrayConst stats = team["statistics"].as<JsonArrayConst>();
      for (JsonObjectConst stat : stats) {
        const String name = String((const char *)(stat["name"] | ""));
        const String displayName = String((const char *)(stat["displayName"] | ""));
        const String value = String((const char *)(stat["displayValue"] | ""));

        if (!name.isEmpty()) {
          applyStatToTeam(*line, name, value);
        }
        if (!displayName.isEmpty()) {
          applyStatToTeam(*line, displayName, value);
        }
      }
    }
  }

  if (io.strengthLabel.isEmpty()) io.strengthLabel = "EVEN STRENGTH";
  return true;
}

bool EspnOlympicClient::fetchLatestGoal(GameState &io, const String &focusTeamAbbr) {
  if (io.gameId.isEmpty()) return false;

  JsonDocument filter;
  filter["plays"][0]["id"] = true;
  filter["plays"][0]["text"] = true;
  filter["plays"][0]["scoringPlay"] = true;
  filter["plays"][0]["team"]["abbreviation"] = true;
  filter["plays"][0]["type"]["text"] = true;
  filter["plays"][0]["participants"][0]["athlete"]["displayName"] = true;

  JsonDocument doc;
  const String url = String(kEspnBase) + "/summary?event=" + io.gameId;
  if (!httpGetJson(url, doc, filter)) return false;

  JsonArrayConst plays = doc["plays"].as<JsonArrayConst>();
  if (plays.isNull() || plays.size() == 0) return false;

  for (int i = (int)plays.size() - 1; i >= 0; --i) {
    JsonObjectConst play = plays[(size_t)i];
    const bool scoringPlay = play["scoringPlay"] | false;
    const String playType = String((const char *)(play["type"]["text"] | ""));
    const bool looksLikeGoal = scoringPlay || strContainsIgnoreCase(playType, "goal");
    if (!looksLikeGoal) continue;

    uint32_t eventId = 0;
    if (play["id"].is<const char *>()) {
      eventId = (uint32_t)strtoul(play["id"].as<const char *>(), nullptr, 10);
    } else {
      eventId = play["id"] | 0;
    }
    if (!eventId) continue;

    const String owner = String((const char *)(play["team"]["abbreviation"] | ""));
    const String text = String((const char *)(play["text"] | ""));
    const String scorer = String((const char *)(play["participants"][0]["athlete"]["displayName"] | ""));

    io.lastGoalEventId = eventId;
    io.goalTeamAbbr = owner;
    io.goalTeamLogoUrl = (owner == io.home.abbr) ? io.home.logoUrl : ((owner == io.away.abbr) ? io.away.logoUrl : "");
    io.goalText = text;
    io.goalScorer = scorer;
    io.focusJustScored = (owner == focusTeamAbbr);

    if (strContainsIgnoreCase(text, "power play")) {
      io.strengthLabel = owner + " POWER PLAY";
    } else if (!io.strengthLabel.endsWith("POWER PLAY")) {
      io.strengthLabel = "EVEN STRENGTH";
    }

    return true;
  }

  return false;
}


