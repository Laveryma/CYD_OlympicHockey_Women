# Audio: O Canada Playback (Women)

This women's firmware can play `O Canada` once per game when the tracked Canada event changes from `pre` to `in`. <--(Or any .wav you choose to upload)

## Required file

Place a WAV file at:

- `/audio/o_canada.wav` in SPIFFS (project path: `data/audio/o_canada.wav` before `uploadfs`)

## Supported WAV format

Use an uncompressed PCM WAV with these settings:

- Mono (`1` channel)
- PCM bit depth: `16-bit` (preferred) or `8-bit` (supported)
- Sample rate: `11025`, `16000`, or `22050` Hz recommended

Other formats (stereo, ADPCM, MP3, float WAV, etc.) are rejected.

## Upload steps

1. Put your file at `data/audio/o_canada.wav`
2. Upload filesystem image:

```powershell
pio run -e esp32-cyd-sdfix -t uploadfs
```

3. Reboot / run firmware

## Trigger behavior

- Plays once when the same game transitions `pre -> in`
- Debounced by game ID (does not replay on feed glitches)
- If device boots while game is already `in`, anthem does not auto-play
- Manual test: hold the BOOT button for ~1.5s to force anthem playback
- While anthem audio is playing, each BOOT click reduces gain by `10` (down to `0`)

## Hardware note

Playback uses `ANTHEM_DAC_PIN` from `include/config.h` (default `GPIO25`).
As currently configured in this repo, `ANTHEM_DAC_PIN_ALT=26` and `ANTHEM_GAIN_PCT=220`.
Set `ANTHEM_DAC_PIN_ALT` to match your board wiring (`25`/`26`) if needed.

