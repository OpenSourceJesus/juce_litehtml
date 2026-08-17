#!/usr/bin/env python3
"""Golden-file regression tests for the headless renderer.

Each case renders a document in a given mode and compares the output against
a recorded .expected file. Because the headless container uses a fixed font
width table, output is deterministic and byte-comparable.

Usage:
    ./run_tests.py             build if needed, then run all cases
    ./run_tests.py --update    rewrite the .expected files from current output
    ./run_tests.py -k table    only run cases whose name matches
    ./run_tests.py --no-build  skip the build step
"""

from __future__ import annotations

import argparse
import difflib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / "build" / "headless" / "litehtml-headless"
TUI_BINARY = ROOT / "build" / "tui" / "litehtml-tui"
DATA = ROOT / "headless" / "testdata"
GOLDEN = ROOT / "headless" / "expected"

# name -> command line arguments (relative to repo root)
CASES = {
    "basic-text":    ["-w", "400", "-m", "text",  str(DATA / "basic.html")],
    "basic-tree":    ["-w", "400", "-m", "tree",  str(DATA / "basic.html")],
    "basic-draw":    ["-w", "400", "-m", "draw",  str(DATA / "basic.html")],
    "basic-ascii":   ["-w", "400", "-m", "ascii", str(DATA / "basic.html")],
    "basic-narrow":  ["-w", "200", "-m", "text",  str(DATA / "basic.html")],
    "basic-wide":    ["-w", "900", "-m", "text",  str(DATA / "basic.html")],
    "table-text":    ["-w", "500", "-m", "text",  str(DATA / "table.html")],
    "table-ascii":   ["-w", "500", "-m", "ascii", str(DATA / "table.html")],
    "table-stats":   ["-w", "500", "-m", "stats", "--image-size", "48x24",
                      str(DATA / "table.html")],
    "script-stats":  ["-m", "stats", "-b", str(DATA), str(DATA / "script.html")],
    "entities-text": ["-w", "400", "-m", "text", str(DATA / "entities.html")],
    # Image sizes come from the file headers, so this pins PNG, JPEG and GIF
    # parsing as well as the layout space they earn.
    "images-draw":   ["-w", "400", "-m", "draw", "-b", str(DATA),
                      str(DATA / "images.html")],
    "images-tree":   ["-w", "400", "-m", "tree", "-b", str(DATA),
                      str(DATA / "images.html")],
}

# The ANSI terminal backend is the testable one: no library to link and it
# works over a pipe, so it stands in for ncurses and notcurses, which share
# every line of code above the blit.
TUI_CASES = {
    "tui-basic-64":   ["-c", "64", "--no-color", str(DATA / "basic.html")],
    "tui-basic-40":   ["-c", "40", "--no-color", str(DATA / "basic.html")],
    "tui-table":      ["-c", "70", "--no-color", str(DATA / "table.html")],
    "tui-entities":   ["-c", "50", "--no-color", str(DATA / "entities.html")],
    "tui-color":      ["-c", "50", "--color", str(DATA / "basic.html")],
    "tui-links":      ["-c", "60", "--links", str(DATA / "page_a.html")],
    "tui-images":     ["-c", "60", "--no-color", str(DATA / "images.html")],
    "tui-page-a":     ["-c", "60", "--no-color", str(DATA / "page_a.html")],
    # Two links, one of them below the fold, which is what exercises
    # scroll-to-focused-link in the interactive backends.
    "tui-links-long": ["-c", "60", "--links", str(DATA / "page_long.html")],
}


def run_case(args, binary=None):
    proc = subprocess.run([str(binary or BINARY)] + args, capture_output=True, text=True)
    out = proc.stdout
    if proc.returncode != 0:
        out += f"\n[exit {proc.returncode}]\n{proc.stderr}"
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="rewrite expected files from current output")
    ap.add_argument("--no-build", action="store_true", help="skip building first")
    ap.add_argument("-k", metavar="PATTERN", help="only run matching cases")
    args = ap.parse_args()

    if not args.no_build:
        for target in ("headless", "tui"):
            rc = subprocess.run([sys.executable, str(ROOT / "build.py"), target]).returncode
            if rc != 0:
                return rc
        print()

    if not BINARY.exists():
        print(f"error: {BINARY} not found", file=sys.stderr)
        return 1

    GOLDEN.mkdir(parents=True, exist_ok=True)

    all_cases = dict(CASES)
    if TUI_BINARY.exists():
        all_cases.update(TUI_CASES)

    names = [n for n in all_cases if not args.k or args.k in n]
    if not names:
        print(f"no cases match '{args.k}'", file=sys.stderr)
        return 1

    passed, failed, written = 0, [], 0

    for name in names:
        binary = TUI_BINARY if name.startswith("tui-") else BINARY
        actual = run_case(all_cases[name], binary)
        golden = GOLDEN / f"{name}.expected"

        if args.update or not golden.exists():
            golden.write_text(actual)
            written += 1
            print(f"  WROTE {name}")
            continue

        expected = golden.read_text()

        if actual == expected:
            passed += 1
            print(f"  ok    {name}")
        else:
            failed.append((name, expected, actual))
            print(f"  FAIL  {name}")

    print()

    for name, expected, actual in failed:
        print(f"--- {name} ---")
        diff = difflib.unified_diff(
            expected.splitlines(keepends=True),
            actual.splitlines(keepends=True),
            fromfile="expected", tofile="actual",
        )
        sys.stdout.writelines(diff)
        print()

    summary = f"{passed} passed, {len(failed)} failed"
    if written:
        summary += f", {written} written"
    print(summary)

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
