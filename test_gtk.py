#!/usr/bin/env python3
"""Checks for the GTK front end that need no display.

`--screenshot` renders offscreen and `--click` follows a link before
rendering, so navigation and image decoding can both be checked in CI. What
this cannot check is the window itself: event plumbing, scrolling and cursor
changes still need a human or an X server.

Usage:
    ./test_gtk.py            build if needed, then run
    ./test_gtk.py --no-build
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / "build" / "gtk" / "litehtml-gtk"
WORK = Path("/tmp/litehtml-tests")
OUT = Path("/tmp/litehtml-gtk-out")


def run(args, timeout=60):
    proc = subprocess.run([str(BINARY)] + args, capture_output=True,
                          text=True, timeout=timeout)
    return proc.returncode, proc.stdout, proc.stderr


def colours(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = im.load()
    seen = {}
    for y in range(h):
        for x in range(w):
            c = px[x, y]
            seen[c] = seen.get(c, 0) + 1
    return im.size, seen


def near(seen, target, tol=6):
    """Counts pixels close to a colour. JPEG is lossy, so exact match fails."""
    total = 0
    for (r, g, b), n in seen.items():
        if abs(r - target[0]) <= tol and abs(g - target[1]) <= tol \
                and abs(b - target[2]) <= tol:
            total += n
    return total


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()

    if not args.no_build:
        if subprocess.run([sys.executable, str(ROOT / "build.py"),
                           "gtk"]).returncode != 0:
            return 1
        print()

    if not BINARY.exists():
        print("error: build the gtk target first", file=sys.stderr)
        return 1

    # The fixtures come from run_tests.py, which owns them.
    if not (WORK / "page_a.html").exists():
        subprocess.run([sys.executable, str(ROOT / "run_tests.py"),
                        "--no-build", "-k", "basic-text"],
                       capture_output=True)

    if not (WORK / "page_a.html").exists():
        print("error: fixtures missing; run ./run_tests.py first", file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)
    checks = []

    def check(name, cond, detail=""):
        checks.append((name, cond, detail))

    try:
        import PIL  # noqa: F401
        have_pil = True
    except ImportError:
        have_pil = False

    png = OUT / "basic.png"
    rc, out, err = run(["--screenshot", str(png), "-w", "500",
                        str(WORK / "basic.html")])
    check("renders offscreen", rc == 0 and png.exists(), err.strip())

    imgs = OUT / "images.png"
    rc, out, err = run(["--screenshot", str(imgs), "-w", "500",
                        str(WORK / "images.html")])
    check("renders a page with images", rc == 0 and imgs.exists(), err.strip())

    if have_pil and imgs.exists():
        size, seen = colours(imgs)
        # The three fixtures are a red png, a blue jpeg and a green gif, so
        # each one present means that decoder ran.
        check("decodes png", near(seen, (200, 60, 60)) > 1000,
              "expected ~2048 red pixels")
        check("decodes jpeg", near(seen, (60, 120, 200)) > 500,
              "expected ~1152 blue pixels")
        check("decodes gif", near(seen, (102, 153, 102), tol=60) > 1000,
              "expected ~3200 green-ish pixels")
    else:
        print("  skip  image colour checks (PIL not installed)")

    # Navigation: click the "Page B" link in page_a.html.
    nav = OUT / "page_b.png"
    rc, out, err = run(["--screenshot", str(nav), "-w", "600",
                        "--click", "70,80", str(WORK / "page_a.html")])
    check("follows a link", rc == 0 and "page_b.html" in out, (out + err).strip())

    nav_c = OUT / "page_c.png"
    rc, out, err = run(["--screenshot", str(nav_c), "-w", "600",
                        "--click", "140,80", str(WORK / "page_a.html")])
    check("follows the right link", rc == 0 and "page_c.html" in out,
          (out + err).strip())

    if nav.exists() and nav_c.exists():
        check("the two pages differ",
              nav.read_bytes() != nav_c.read_bytes())

    rc, out, err = run(["--screenshot", str(OUT / "none.png"), "-w", "600",
                        "--click", "400,300", str(WORK / "page_a.html")])
    check("reports a click that hits no link", rc != 0 and "no link" in err,
          err.strip())

    rc, out, err = run(["--screenshot", str(OUT / "x.png"),
                        "http://127.0.0.1:1/nope.html"])
    check("refuses the network by default", rc != 0 and "disabled" in err,
          err.strip())

    failed = 0
    for name, ok, detail in checks:
        print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
        if not ok:
            failed += 1
            if detail:
                print(f"        {detail}")

    print()
    print(f"{len(checks) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
