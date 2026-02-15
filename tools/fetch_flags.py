#!/usr/bin/env python3
"""Fetch Olympic hockey country flags from ESPN into data/flags for SPIFFS upload.

Usage (PowerShell):
  python tools/fetch_flags.py
  python tools/fetch_flags.py --start 20260101 --end 20260222 --out data/flags
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import urllib.error
import urllib.request
from typing import Dict

SCOREBOARD_URL = (
    "https://site.api.espn.com/apis/site/v2/sports/hockey/"
    "olympics-womens-ice-hockey/scoreboard?dates={start}-{end}"
)

DEFAULT_START = "20260101"
DEFAULT_END = "20260222"
SIZES = (56, 64, 96)


def fetch_json(url: str) -> dict:
    req = urllib.request.Request(url, headers={"User-Agent": "flag-fetcher/1.0"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode("utf-8"))


def download_file(url: str, path: str) -> bool:
    req = urllib.request.Request(url, headers={"User-Agent": "flag-fetcher/1.0", "Accept": "image/png"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
    except urllib.error.URLError as exc:
        print(f"  ! {url} -> {exc}")
        return False

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return True


def rewrite_size(url: str, size: int) -> str:
    # ESPN country logos are provided at a canonical size (often /500/).
    # Use the ESPN combiner endpoint to produce small PNGs for SPIFFS.
    path = re.sub(r"^https?://[^/]+", "", url)
    path = path.split("?", 1)[0]
    return f"https://a.espncdn.com/combiner/i?img={path}&w={size}&h={size}"


def collect_flags(payload: dict) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for ev in payload.get("events", []):
        competitions = ev.get("competitions", [])
        if not competitions:
            continue
        competitors = competitions[0].get("competitors", [])
        for comp in competitors:
            team = comp.get("team", {})
            abbr = team.get("abbreviation", "").strip().upper()
            logo = team.get("logo", "").strip()
            if abbr and logo:
                out[abbr] = logo
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch ESPN Olympic country flags into data/flags")
    parser.add_argument("--start", default=DEFAULT_START, help="start date YYYYMMDD")
    parser.add_argument("--end", default=DEFAULT_END, help="end date YYYYMMDD")
    parser.add_argument("--out", default=os.path.join("data", "flags"), help="output root folder")
    args = parser.parse_args()

    url = SCOREBOARD_URL.format(start=args.start, end=args.end)
    print(f"Fetching schedule: {url}")

    try:
        payload = fetch_json(url)
    except Exception as exc:  # noqa: BLE001
        print(f"Failed to fetch scoreboard JSON: {exc}")
        return 1

    flags = collect_flags(payload)
    if not flags:
        print("No teams/logos found in payload")
        return 1

    print(f"Found {len(flags)} teams")

    ok_count = 0
    for abbr in sorted(flags):
        logo_url = flags[abbr]
        print(f"- {abbr}")
        for size in SIZES:
            sized_url = rewrite_size(logo_url, size)
            out_path = os.path.join(args.out, str(size), f"{abbr}.png")
            if download_file(sized_url, out_path):
                ok_count += 1

        # Canonical cache key expected by runtime fallback.
        src96 = os.path.join(args.out, "96", f"{abbr}.png")
        flat = os.path.join(args.out, f"{abbr}.png")
        if os.path.exists(src96):
            os.makedirs(os.path.dirname(flat), exist_ok=True)
            shutil.copyfile(src96, flat)

    print(f"Done. Downloaded {ok_count} sized flag files into {args.out}")
    print("Upload to SPIFFS with: pio run -e esp32-cyd-sdfix -t uploadfs")
    return 0


if __name__ == "__main__":
    sys.exit(main())

