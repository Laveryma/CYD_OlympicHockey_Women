# CYD Olympic Hockey Scoreboard (Women)

ESP32-2432S028 (CYD) scoreboard for **Milano Cortina 2026 women's hockey**.

<img width="1374" height="746" alt="WomenGameDay" src="https://github.com/user-attachments/assets/b1f9f8d9-14d4-4920-a4f5-0cd7b9fddb46" /><img width="567" height="403" alt="Live" src="https://github.com/user-attachments/assets/6eb5cefd-22ab-4444-a3db-816a199f9a63" />


## What it does

- Uses ESPN Olympic women's hockey JSON feed
- Default to focus on `CAN` (Team Canada women) <-- Easily updated to any participating nation (see `## Config` section below)
- Auto-selects event priority:
  - in-progress Canada (or user defined nation) game
  - else next scheduled Canada (or user defined nation) game (countdown)
  - else most recent completed Canada (or user defined nation) game
- Screens:
  - `NEXT_GAME` (merged no-game + pre-game)
  - `LIVE`
  - `INTERMISSION`
  - `FINAL`
  - `GOAL` (when detectable from summary plays)
  - `LAST_GAME`
  - `STANDINGS` (group tables)
- Builds group standings from completed Preliminary Round games
- Loads country flags from SPIFFS (`/flags/...`), with runtime URL cache fallback
- Optional anthem playback at puck drop transition (`pre -> in`)

## Locked build environment

Use this exact env (pinned platform/libs):

```powershell
pio run -e esp32-cyd-sdfix
```

Upload firmware:

```powershell
pio run -e esp32-cyd-sdfix -t upload
```

Upload SPIFFS assets:

```powershell
pio run -e esp32-cyd-sdfix -t uploadfs
```

## Config

Edit `include/config.h`:

- Update Wi-Fi credentials to your own (optional fallback credential may be included as well)
- `FOCUS_TEAM_ABBR` (default `CAN`, or update to favorite country NOC code, e.g. `CAN`, `USA`, `NOR`, `CZE`, etc.))
- `TZ_INFO` for local countdown display
- `ANTHEM_DAC_PIN` (default `25`)

## Flags in SPIFFS

Preferred paths:

- `/flags/56/CAN.png`
- `/flags/64/CAN.png`
- `/flags/96/CAN.png`
- optional canonical fallback `/flags/CAN.png`

*If favourite nation flag is not listed in `data/flags/`, run the included fetch flags tool (`tools/fetch_flags.py`) or find your own flag image, resize to `56px`, `64px`, `96px`, then save to appropriate `data/flags` folders as `<NOC>.png`

Generate/download flags from ESPN:

```powershell
python tools/fetch_flags.py
```

Then upload SPIFFS:

```powershell
pio run -e esp32-cyd-sdfix -t uploadfs
```

## Data source

Primary schedule/scoreboard endpoint:

- `https://site.api.espn.com/apis/site/v2/sports/hockey/olympics-womens-ice-hockey/scoreboard?dates=YYYYMMDD-YYYYMMDD`

Optional detail endpoint used for stats/goal detection:

- `https://site.api.espn.com/apis/site/v2/sports/hockey/olympics-womens-ice-hockey/summary?event=<eventId>`

## Audio

See `README_AUDIO.md` for WAV format and upload instructions.

## Support

If you enjoy what I’m making and want to support more late-night builds, experiments, and ideas turning into reality, it's genuinely appreciated.

- https://buymeacoffee.com/zerocypherxiii





