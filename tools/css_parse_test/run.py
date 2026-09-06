#!/usr/bin/env python3
"""CSS parse regression checks via the headless renderer.

Asserts on draw/tree output for bugs that used to paint wrong or hide
whole subtrees (gradient→black, :is() comma-split → table{display:none},
flex→inline collapse, brace mismatch truncating sheets, sticky→fixed).

Usage:
    python3 tools/css_parse_test/run.py
    make test-css
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BIN = ROOT / "build" / "headless" / "litehtml-headless"
FIX = Path(__file__).resolve().parent / "fixtures"


def run(mode: str, width: int, html: Path, height: int | None = None) -> str:
    argv = [str(BIN), "-w", str(width), "-m", mode, str(html)]
    if height is not None:
        argv[1:1] = ["-h", str(height)]
    proc = subprocess.run(argv, capture_output=True, text=True, timeout=60)
    out = proc.stdout
    if proc.returncode != 0:
        out += f"\n[exit {proc.returncode}]\n{proc.stderr}"
    return out


def must(hay: str, pat: str, label: str) -> None:
    if not re.search(pat, hay, re.M):
        raise AssertionError(f"{label}: expected /{pat}/ in:\n{hay}")


def must_not(hay: str, pat: str, label: str) -> None:
    if re.search(pat, hay, re.M):
        raise AssertionError(f"{label}: forbidden /{pat}/ in:\n{hay}")


def test_colors() -> None:
    out = run("draw", 300, FIX / "colors.html")
    # linear-gradient must not become opaque black fill
    must_not(out, r"background \([^)]*200x16\) #000000ff", "gradient→black")
    must(out, r"#008000ff", "hsl text")
    must(out, r"#0000ff80", "hsla background")
    must(out, r"#11223344", "8-digit hex")
    must(out, r"#ff0000ff", "rgb()")


def test_is_comma() -> None:
    out = run("draw", 200, FIX / "is_comma.html")
    must(out, r"#00ff00ff", ":is() comma-split hid table")
    must_not(out, r"background \([^)]*\) #000000ff", "table painted black")


def test_flex_row() -> None:
    out = run("tree", 400, FIX / "flex_row.html")
    # row children become inline-block; column kids stay block
    must(out, r"div \[inline-block\]", "flex row uses inline-block kids")
    # Three inline-block boxes on the same y, ascending x (A B C)
    boxes = re.findall(r"div \[inline-block\] \((\d+),(\d+) (\d+)x(\d+)\)", out)
    if len(boxes) < 3:
        raise AssertionError(f"expected ≥3 inline-block kids, got {boxes}\n{out}")
    xs = [int(b[0]) for b in boxes[:3]]
    ys = [int(b[1]) for b in boxes[:3]]
    if len(set(ys)) != 1:
        raise AssertionError(f"flex row kids not same y: {boxes[:3]}")
    if not (xs[0] < xs[1] < xs[2]):
        raise AssertionError(f"flex row kids not left-to-right: {boxes[:3]}")
    # column: block children stacked (different y)
    must(out, r'\[inline-text\][^\n]*"X"', "flex column X")
    must(out, r'\[inline-text\][^\n]*"Y"', "flex column Y")
    # After the row's inline-blocks, column divs should be [block]
    col = re.findall(r"div \[block\] \((\d+),(\d+) (\d+)x(\d+)\)", out)
    # Need at least the col container + X + Y (row container is also block)
    if len(col) < 3:
        raise AssertionError(f"expected stacked block boxes for column, got {col}\n{out}")


def test_braces() -> None:
    out = run("draw", 200, FIX / "braces.html")
    must(out, r"#0000ffff", "@media brace match dropped later rule")


def test_sticky() -> None:
    # sticky→static: bar sits in normal flow under .pad (y > 0), not fixed at 0.
    out = run("draw", 300, FIX / "sticky.html", height=200)
    m = re.search(r"background \((\d+),(\d+) (\d+)x(\d+)\) #ff0000ff", out)
    if not m:
        raise AssertionError(f"sticky bar geometry missing:\n{out}")
    y = int(m.group(2))
    if y < 20:
        raise AssertionError(
            f"sticky bar at y={y}: looks fixed/overlay, expected in-flow under pad"
        )


def test_mp_columns() -> None:
    """Wikipedia Main Page: #mp-upper flex + flex-basis % → side-by-side."""
    out = run("tree", 800, FIX / "mp_columns.html")
    left = re.search(r'\[inline-text\] \((\d+),(\d+)[^"\n]*"From"', out)
    right = re.search(r'\[inline-text\] \((\d+),(\d+)[^"\n]*"In"', out)
    if not left or not right:
        raise AssertionError(f"column headings missing:\n{out}")
    lx, ly = int(left.group(1)), int(left.group(2))
    rx, ry = int(right.group(1)), int(right.group(2))
    if ly != ry:
        raise AssertionError(f"columns not same row: From@({lx},{ly}) In@({rx},{ry})")
    if rx <= lx:
        raise AssertionError(f"In the news not to the right of featured: {lx} vs {rx}")


TESTS = [
    ("colors", test_colors),
    ("is_comma", test_is_comma),
    ("flex_row", test_flex_row),
    ("braces", test_braces),
    ("sticky", test_sticky),
    ("mp_columns", test_mp_columns),
]


def main() -> int:
    if not BIN.is_file():
        print(f"error: missing {BIN} (run: make headless)", file=sys.stderr)
        return 1

    failed = 0
    for name, fn in TESTS:
        try:
            fn()
            print(f"PASS  {name}")
        except AssertionError as e:
            failed += 1
            print(f"FAIL  {name}: {e}", file=sys.stderr)
        except Exception as e:
            failed += 1
            print(f"FAIL  {name}: {type(e).__name__}: {e}", file=sys.stderr)

    print(f"{len(TESTS) - failed}/{len(TESTS)} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
