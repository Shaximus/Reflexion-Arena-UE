#!/usr/bin/env python3
"""ARM-04 item 7/8 — turn a playtest log into a machine-checkable verdict.

Usage:  verify_playtest.py <playtest.stdout> [--expect-map RxTestMap] [--json out.json]

Exists because the process exit code is NOT a health signal for this project:
a headless -game session never self-exits (terminated by `timeout`, exit 124), and
the pythonscript commandlet returns 1 even on success because unrelated engine
plugins log errors every run. The log is the signal. This reads it.

Exit codes:  0 = all required signals present   1 = at least one missing   2 = usage/IO error
"""

import argparse
import json
import os
import re
import sys

TICK_RE = re.compile(r"^\[[0-9.\-:]+\]\[\s*(\d+)\]")


def analyse(path, expect_map):
    with open(path, "r", errors="replace") as f:
        lines = f.readlines()

    text = "".join(lines)
    ticks = [int(m.group(1)) for m in (TICK_RE.match(l) for l in lines) if m]

    signals = {
        "map_loaded": "LogGlobalStatus: LoadMap Load map complete /Game/Maps/%s" % expect_map in text
                      or "Load map complete /Game/Maps/%s" % expect_map in text,
        "gamemode_is_project_gamemode": "Game class is 'BP_ThirdPersonGameMode_C'" in text,
        "player_spawned_at_playerstart": "RestartPlayerAtPlayerStart" in text,
        "world_brought_up_for_play": "up for play" in text,
        "engine_ticked": len(ticks) > 0 and max(ticks) > 0,
    }

    counts = {
        "restart_player_count": text.count("RestartPlayerAtPlayerStart"),
        "error_lines": len(re.findall(r"\bError:", text)),
        "max_tick_index": max(ticks) if ticks else 0,
        "total_lines": len(lines),
        "fell_out_of_world_hits": len(re.findall(r"fell out of the world|OutOfWorld", text, re.I)),
    }

    return {
        "log": os.path.abspath(path),
        "expect_map": expect_map,
        "signals": signals,
        "counts": counts,
        "ok": all(signals.values()),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--expect-map", default="RxTestMap")
    ap.add_argument("--json")
    args = ap.parse_args()

    if not os.path.exists(args.log):
        print("MISSING LOG: %s" % args.log, file=sys.stderr)
        return 2

    r = analyse(args.log, args.expect_map)
    for name, ok in r["signals"].items():
        print("  %s %s" % ("PASS" if ok else "FAIL", name))
    for k, v in r["counts"].items():
        print("  count %s = %s" % (k, v))
    print("VERDICT ok=%s" % r["ok"])

    if args.json:
        with open(args.json, "w") as f:
            json.dump(r, f, indent=2)
        print("wrote %s" % args.json)

    return 0 if r["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
