#!/usr/bin/env python3
"""Golden-file regression tests for the headless renderer and the ANSI
terminal backend.

Self-contained: the documents under test and their expected output are both
embedded below, and the fixtures are written to a scratch directory under
/tmp on each run. Nothing outside this file is needed, so there is no
directory of fixtures to keep in sync with the tests that read it.

Output is comparable at all because the headless container computes text
metrics from a fixed width table rather than from system fonts, so the same
input produces byte-identical output on any machine at any optimisation level.

Usage:
    ./run_tests.py             build if needed, then run every case
    ./run_tests.py --update    rewrite the expected blocks in this file
    ./run_tests.py -k table    only run cases whose name matches
    ./run_tests.py --no-build  skip the build step
    ./run_tests.py --keep      leave the scratch directory in place
"""

from __future__ import annotations

import argparse
import difflib
import re
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / "build" / "headless" / "litehtml-headless"
TUI_BINARY = ROOT / "build" / "tui" / "litehtml-tui"

# A fixed location rather than a random temporary directory: a failing case is
# far easier to investigate when the fixture it used is still where it was,
# under a name you can guess.
WORK = Path("/tmp/litehtml-tests")

# Images are referenced by three documents. Only the dimensions matter here --
# nothing in this suite decodes them, it only reads their headers -- so they
# are generated rather than stored.
IMAGES = {
    "logo.png": ("png", 64, 32, (200, 60, 60)),
    "photo.jpg": ("jpeg", 48, 24, (60, 120, 200)),
    "anim.gif": ("gif", 80, 40, (90, 160, 90)),
}

# ---------------------------------------------------------------------------
# Fixtures
#
# NOTE: trailing spaces are significant in the terminal output below (a cell
# with a background is not blank), so they are written as \x20 where they
# occur. Do not "clean up" those escapes.
# ---------------------------------------------------------------------------

TEXT_FILES = {
    "app.js": """\
console.log("hello from app.js");
""",
    "basic.html": """\
<!DOCTYPE html>
<html>
<head>
  <title>Headless smoke test</title>
  <style>
    body { font-family: sans-serif; font-size: 16px; margin: 8px; }
    h1 { font-size: 28px; font-weight: bold; }
    .box { border: 2px solid #333; padding: 6px; width: 300px; }
    .muted { color: #777; font-style: italic; }
    ul { margin-left: 20px; }
  </style>
</head>
<body>
  <h1>Hello, headless</h1>
  <p>This paragraph should wrap onto several lines once it gets long enough to exceed the viewport width that we hand to the renderer.</p>
  <div class="box">
    <p class="muted">Inside a bordered box.</p>
  </div>
  <ul>
    <li>First item</li>
    <li>Second item</li>
  </ul>
  <p><a href="https://example.com">A link</a> and <b>bold</b> and <code>code</code>.</p>
</body>
</html>
""",
    "entities.html": """\
<html><body>
<p>Entities: &amp; &lt; &gt; &quot; &nbsp; &copy; &mdash;</p>
<p>Unicode: caf&eacute;, na&iuml;ve, 日本語テキスト, emoji &#128512;</p>
<p style="text-transform: uppercase">shouting</p>
<p style="text-transform: capitalize">title case here</p>
</body></html>
""",
    "images.html": """\
<html><body><p>A<img src="logo.png">B</p><p><img src="photo.jpg"></p><p><img src="anim.gif"></p></body></html>""",
    "page_a.html": """\
<html><head><title>Page A</title></head><body>
<h1>Page A</h1>
<p>Go to <a href="page_b.html">Page B</a> or <a href="page_c.html">Page C</a>.</p>
<p>External: <a href="https://example.com">example</a>, fragment: <a href="#top">top</a>.</p>
<p><img src="logo.png" alt="logo"></p>
</body></html>
""",
    "page_b.html": """\
<html><head><title>Page B</title></head><body>
<h1>Page B</h1><p>This is the second page. <a href="page_a.html">Back to A</a></p>
</body></html>
""",
    "page_c.html": """\
<html><head><title>Page C</title></head><body><h1>Page C</h1><p>Third page. <a href="page_a.html">Home</a></p></body></html>
""",
    "page_long.html": """\
<html><head><title>Long Page</title></head><body>
<h1>Long Page</h1>
<p><a href="missing_file.html">Broken link</a></p>
<p>Filler paragraph number 1, here to push content down the page.</p>
<p>Filler paragraph number 2, here to push content down the page.</p>
<p>Filler paragraph number 3, here to push content down the page.</p>
<p>Filler paragraph number 4, here to push content down the page.</p>
<p>Filler paragraph number 5, here to push content down the page.</p>
<p>Filler paragraph number 6, here to push content down the page.</p>
<p>Filler paragraph number 7, here to push content down the page.</p>
<p>Filler paragraph number 8, here to push content down the page.</p>
<p>Filler paragraph number 9, here to push content down the page.</p>
<p>Filler paragraph number 10, here to push content down the page.</p>
<p>Filler paragraph number 11, here to push content down the page.</p>
<p>Bottom: <a href="page_b.html">Page B at the end</a></p>
</body></html>
""",
    "script.html": """\
<html><head><title>Scripts</title><script src="app.js"></script></head>
<body><script>var inline = 1;</script><p>body text</p></body></html>
""",
    "site.css": """\
body { margin: 0; font-size: 14px; }
table { border-collapse: collapse; }
td, th { border: 1px solid #999; padding: 4px; }
th { font-weight: bold; background: #eee; }
.wide { width: 200px; }
""",
    "table.html": """\
<!DOCTYPE html>
<html>
<head>
  <title>Table and assets</title>
  <link rel="stylesheet" href="site.css">
</head>
<body>
  <table>
    <tr><th>Name</th><th class="wide">Description</th></tr>
    <tr><td>alpha</td><td>first row of the table</td></tr>
    <tr><td>beta</td><td>second row</td></tr>
  </table>
  <p><img src="logo.png" alt="logo"> after image</p>
</body>
</html>
""",
}

# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

def data(name):
    return str(WORK / name)


def cases():
    """Returns {name: (binary_key, argv)}."""
    return {
        # -- headless renderer ------------------------------------------
        "basic-text":     ("headless", ["-w", "400", "-m", "text", data("basic.html")]),
        "basic-tree":     ("headless", ["-w", "400", "-m", "tree", data("basic.html")]),
        "basic-draw":     ("headless", ["-w", "400", "-m", "draw", data("basic.html")]),
        "basic-ascii":    ("headless", ["-w", "400", "-m", "ascii", data("basic.html")]),
        "basic-narrow":   ("headless", ["-w", "200", "-m", "text", data("basic.html")]),
        "basic-wide":     ("headless", ["-w", "900", "-m", "text", data("basic.html")]),
        "table-text":     ("headless", ["-w", "500", "-m", "text", data("table.html")]),
        "table-ascii":    ("headless", ["-w", "500", "-m", "ascii", data("table.html")]),
        "table-stats":    ("headless", ["-w", "500", "-m", "stats", "--image-size",
                                        "48x24", data("table.html")]),
        "script-stats":   ("headless", ["-m", "stats", "-b", str(WORK),
                                        data("script.html")]),
        "entities-text":  ("headless", ["-w", "400", "-m", "text", data("entities.html")]),
        # Image sizes come from the file headers, so these pin PNG, JPEG and
        # GIF parsing as well as the layout space they earn.
        "images-draw":    ("headless", ["-w", "400", "-m", "draw", "-b", str(WORK),
                                        data("images.html")]),
        "images-tree":    ("headless", ["-w", "400", "-m", "tree", "-b", str(WORK),
                                        data("images.html")]),

        # -- ANSI terminal backend --------------------------------------
        # The testable terminal backend: nothing to link and it works over a
        # pipe, so it stands in for ncurses and notcurses, which share every
        # line of code above the blit.
        "tui-basic-64":   ("tui", ["-c", "64", "--no-color", data("basic.html")]),
        "tui-basic-40":   ("tui", ["-c", "40", "--no-color", data("basic.html")]),
        "tui-table":      ("tui", ["-c", "70", "--no-color", data("table.html")]),
        "tui-entities":   ("tui", ["-c", "50", "--no-color", data("entities.html")]),
        "tui-color":      ("tui", ["-c", "50", "--color", data("basic.html")]),
        "tui-links":      ("tui", ["-c", "60", "--links", data("page_a.html")]),
        "tui-images":     ("tui", ["-c", "60", "--no-color", data("images.html")]),
        "tui-page-a":     ("tui", ["-c", "60", "--no-color", data("page_a.html")]),
        "tui-links-long": ("tui", ["-c", "60", "--links", data("page_long.html")]),
    }


EXPECTED = {
    "basic-ascii": """\

 Hello,        headless



 This  paragraph   should   wrap  onto  several   lines once  it
 gets  long enough    to exceed   the viewport   width  that we

 hand   to the renderer.

 |---------------------------------------------------|
 |                                                   |
 |Inside  a bordered    box.                         |
 |                                                   |
 |                                                   |
 |                                                   |
 |---------------------------------------------------|
        *  First item
           Second    item
        *

 A   link and bold and   code  .




























""",
    "basic-draw": """\
[0] borders (8,130 316x64) #333333ff
[1] marker (52,215 6x6) #000000ff
[2] marker (52,231 6x6) #000000ff
[3] text (8,19 78x28) #000000ff sans-serif/28/bold "Hello,"
[4] text (95,19 121x28) #000000ff sans-serif/28/bold "headless"
[5] text (8,66 30x16) #000000ff sans-serif/16 "This"
[6] text (42,66 73x16) #000000ff sans-serif/16 "paragraph"
[7] text (119,66 47x16) #000000ff sans-serif/16 "should"
[8] text (170,66 35x16) #000000ff sans-serif/16 "wrap"
[9] text (209,66 31x16) #000000ff sans-serif/16 "onto"
[10] text (244,66 52x16) #000000ff sans-serif/16 "several"
[11] text (300,66 33x16) #000000ff sans-serif/16 "lines"
[12] text (337,66 35x16) #000000ff sans-serif/16 "once"
[13] text (376,66 8x16) #000000ff sans-serif/16 "it"
[14] text (8,82 30x16) #000000ff sans-serif/16 "gets"
[15] text (42,82 30x16) #000000ff sans-serif/16 "long"
[16] text (76,82 53x16) #000000ff sans-serif/16 "enough"
[17] text (133,82 13x16) #000000ff sans-serif/16 "to"
[18] text (150,82 52x16) #000000ff sans-serif/16 "exceed"
[19] text (206,82 22x16) #000000ff sans-serif/16 "the"
[20] text (232,82 60x16) #000000ff sans-serif/16 "viewport"
[21] text (296,82 37x16) #000000ff sans-serif/16 "width"
[22] text (337,82 27x16) #000000ff sans-serif/16 "that"
[23] text (368,82 20x16) #000000ff sans-serif/16 "we"
[24] text (8,98 36x16) #000000ff sans-serif/16 "hand"
[25] text (48,98 13x16) #000000ff sans-serif/16 "to"
[26] text (65,98 22x16) #000000ff sans-serif/16 "the"
[27] text (91,98 65x16) #000000ff sans-serif/16 "renderer."
[28] text (16,154 43x16) #777777ff sans-serif/16/italic "Inside"
[29] text (63,154 9x16) #777777ff sans-serif/16/italic "a"
[30] text (76,154 64x16) #777777ff sans-serif/16/italic "bordered"
[31] text (144,154 30x16) #777777ff sans-serif/16/italic "box."
[32] text (68,210 31x16) #000000ff sans-serif/16 "First"
[33] text (103,210 30x16) #000000ff sans-serif/16 "item"
[34] text (68,226 54x16) #000000ff sans-serif/16 "Second"
[35] text (126,226 30x16) #000000ff sans-serif/16 "item"
[36] text (8,258 11x16) #0000ffff sans-serif/16 "A"
[37] text (19,258 4x16) #0000ffff sans-serif/16 ""
[38] text (23,258 24x16) #0000ffff sans-serif/16 "link"
[39] text (51,258 27x16) #000000ff sans-serif/16 "and"
[40] text (82,258 33x16) #000000ff sans-serif/16/bold "bold"
[41] text (119,258 27x16) #000000ff sans-serif/16 "and"
[42] text (150,258 38x16) #000000ff monospace/16 "code"
[43] text (188,258 4x16) #000000ff sans-serif/16 "."
""",
    "basic-narrow": """\
Hello,
headless
This paragraph should
wrap onto several lines
once it gets long enough
to exceed the viewport
width that we hand to the
renderer.
Inside a bordered box.
• First item
• Second item
A link and bold and code.
""",
    "basic-text": """\
Hello, headless
This paragraph should wrap onto several lines once it
gets long enough to exceed the viewport width that we
hand to the renderer.
Inside a bordered box.
• First item
• Second item
A link and bold and code.
""",
    "basic-tree": """\
html [block] (0,0 400x600)
  head [none] (0,0 0x0)
     [inline-text] (0,0 0x0)
     [inline-text] (0,0 0x0)
     [inline-text] (0,0 0x0)
    title [none] (0,0 0x0)
       [inline-text] (0,0 0x0) "Headless"
       [inline-text] (0,0 0x0)
       [inline-text] (0,0 0x0) "smoke"
       [inline-text] (0,0 0x0)
       [inline-text] (0,0 0x0) "test"
     [inline-text] (0,0 0x0)
     [inline-text] (0,0 0x0)
     [inline-text] (0,0 0x0)
    style [none] (0,0 0x0)
     [inline-text] (0,0 0x0)
   [inline-text] (0,0 4x16)
  body [block] (8,19 384x565)
     [inline-text] (8,19 4x16)
     [inline-text] (8,19 0x0)
     [inline-text] (8,19 0x0)
    h1 [block] (8,19 384x28)
       [inline-text] (8,19 78x28) "Hello,"
       [inline-text] (86,19 9x28)
       [inline-text] (95,19 121x28) "headless"
     [inline-text] (8,19 4x16)
     [inline-text] (8,19 0x0)
     [inline-text] (8,19 0x0)
    p [block] (8,66 384x48)
       [inline-text] (8,66 30x16) "This"
       [inline-text] (38,66 4x16)
       [inline-text] (42,66 73x16) "paragraph"
       [inline-text] (115,66 4x16)
       [inline-text] (119,66 47x16) "should"
       [inline-text] (166,66 4x16)
       [inline-text] (170,66 35x16) "wrap"
       [inline-text] (205,66 4x16)
       [inline-text] (209,66 31x16) "onto"
       [inline-text] (240,66 4x16)
       [inline-text] (244,66 52x16) "several"
       [inline-text] (296,66 4x16)
       [inline-text] (300,66 33x16) "lines"
       [inline-text] (333,66 4x16)
       [inline-text] (337,66 35x16) "once"
       [inline-text] (372,66 4x16)
       [inline-text] (376,66 8x16) "it"
       [inline-text] (384,66 4x16)
       [inline-text] (8,82 30x16) "gets"
       [inline-text] (38,82 4x16)
       [inline-text] (42,82 30x16) "long"
       [inline-text] (72,82 4x16)
       [inline-text] (76,82 53x16) "enough"
       [inline-text] (129,82 4x16)
       [inline-text] (133,82 13x16) "to"
       [inline-text] (146,82 4x16)
       [inline-text] (150,82 52x16) "exceed"
       [inline-text] (202,82 4x16)
       [inline-text] (206,82 22x16) "the"
       [inline-text] (228,82 4x16)
       [inline-text] (232,82 60x16) "viewport"
       [inline-text] (292,82 4x16)
       [inline-text] (296,82 37x16) "width"
       [inline-text] (333,82 4x16)
       [inline-text] (337,82 27x16) "that"
       [inline-text] (364,82 4x16)
       [inline-text] (368,82 20x16) "we"
       [inline-text] (388,82 4x16)
       [inline-text] (8,98 36x16) "hand"
       [inline-text] (44,98 4x16)
       [inline-text] (48,98 13x16) "to"
       [inline-text] (61,98 4x16)
       [inline-text] (65,98 22x16) "the"
       [inline-text] (87,98 4x16)
       [inline-text] (91,98 65x16) "renderer."
     [inline-text] (8,19 4x16)
     [inline-text] (8,19 0x0)
     [inline-text] (8,19 0x0)
    div [block] (16,138 300x48)
       [inline-text] (16,138 4x16)
       [inline-text] (16,138 0x0)
       [inline-text] (16,138 0x0)
       [inline-text] (16,138 0x0)
       [inline-text] (16,138 0x0)
      p [block] (16,154 300x16)
         [inline-text] (16,154 43x16) "Inside"
         [inline-text] (59,154 4x16)
         [inline-text] (63,154 9x16) "a"
         [inline-text] (72,154 4x16)
         [inline-text] (76,154 64x16) "bordered"
         [inline-text] (140,154 4x16)
         [inline-text] (144,154 30x16) "box."
       [inline-text] (16,138 4x16)
       [inline-text] (16,138 0x0)
       [inline-text] (16,138 0x0)
     [inline-text] (8,19 4x16)
     [inline-text] (8,19 0x0)
     [inline-text] (8,19 0x0)
    ul [block] (68,210 324x32)
       [inline-text] (68,210 4x16)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
      li [list-item] (68,210 324x16)
         [inline-text] (68,210 31x16) "First"
         [inline-text] (99,210 4x16)
         [inline-text] (103,210 30x16) "item"
       [inline-text] (68,210 4x16)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
      li [list-item] (68,226 324x16)
         [inline-text] (68,226 54x16) "Second"
         [inline-text] (122,226 4x16)
         [inline-text] (126,226 30x16) "item"
       [inline-text] (68,210 4x16)
       [inline-text] (68,210 0x0)
       [inline-text] (68,210 0x0)
     [inline-text] (8,19 4x16)
     [inline-text] (8,19 0x0)
     [inline-text] (8,19 0x0)
    p [block] (8,258 384x16)
      a [inline] (8,258 0x0)
         [inline-text] (8,258 11x16) "A"
         [inline-text] (19,258 4x16)
         [inline-text] (23,258 24x16) "link"
       [inline-text] (47,258 4x16)
       [inline-text] (51,258 27x16) "and"
       [inline-text] (78,258 4x16)
      b [inline] (8,258 0x0)
         [inline-text] (82,258 33x16) "bold"
       [inline-text] (115,258 4x16)
       [inline-text] (119,258 27x16) "and"
       [inline-text] (146,258 4x16)
      code [inline] (8,258 0x0)
         [inline-text] (150,258 38x16) "code"
       [inline-text] (188,258 4x16) "."
     [inline-text] (8,19 4x16)
     [inline-text] (8,19 0x0)
     [inline-text] (8,19 0x0)
""",
    "basic-wide": """\
Hello, headless
This paragraph should wrap onto several lines once it gets long enough to exceed the viewport width that we hand to the
renderer.
Inside a bordered box.
• First item
• Second item
A link and bold and code.
""",
    "entities-text": """\
Entities: & < > "   © —
Unicode: café, naïve, 日本語テキスト, emoji 😀
SHOUTING
Title Case Here
""",
    "images-draw": """\
[0] text (8,35 11x16) #000000ff sans-serif/16 "A"
[1] background (19,16 64x32) #00000000 "logo.png"
[2] borders (19,16 64x32) #000000ff
[3] text (83,35 11x16) #000000ff sans-serif/16 "B"
[4] background (8,67 48x24) #00000000 "photo.jpg"
[5] borders (8,67 48x24) #000000ff
[6] background (8,110 80x40) #00000000 "anim.gif"
[7] borders (8,110 80x40) #000000ff
""",
    "images-tree": """\
html [block] (0,0 400x600)
  head [none] (0,0 0x0)
  body [block] (8,16 384x568)
    p [block] (8,16 384x35)
       [inline-text] (8,35 11x16) "A"
      img [inline-block] (19,16 64x32)
       [inline-text] (83,35 11x16) "B"
    p [block] (8,67 384x27)
      img [inline-block] (8,67 48x24)
    p [block] (8,110 384x43)
      img [inline-block] (8,110 80x40)
""",
    "script-stats": """\
title:        Scripts
size:         800x600
fonts:        1
draw calls:   2
images:       0
scripts:      1
""",
    "table-ascii": """\
Name----|Description---------------------|
|-------|--------------------------------|
alpha   |first row of the table          |
|-------|--------------------------------|
beta    |second  row                     |
|-------|--------------------------------|

|---------|
|         |after image
|---------|








































""",
    "table-stats": """\
title:        Table and assets
size:         500x600
fonts:        3
draw calls:   23
images:       1
scripts:      0
""",
    "table-text": """\
Name Description
alpha first row of the table
beta second row
after image
""",
    "tui-basic-40": """\

 Hello, headless


 This paragraph should wrap onto
 several lines once it gets long enough
 to exceed the viewport width that we
 hand to the renderer.

 ┌──────────────────────────────────────
 │Inside a bordered box.
 │
 └──────────────────────────────────────

      • First item
      • Second item

 A link and bold and code.
""",
    "tui-basic-64": """\

 Hello, headless


 This paragraph should wrap onto several lines once it gets
 long enough to exceed the viewport width that we hand to the
 renderer.

 ┌──────────────────────────────────────┐
 │Inside a bordered box.                │
 │                                      │
 └──────────────────────────────────────┘

      • First item
      • Second item

 A link and bold and code.
""",
    "tui-color": """\

 [0m[1m[38;2;0;0;0mHello,[0m [0m[1m[38;2;0;0;0mheadless[0m


 [38;2;0;0;0mThis[39m [38;2;0;0;0mparagraph[39m [38;2;0;0;0mshould[39m [38;2;0;0;0mwrap[39m [38;2;0;0;0monto[39m [38;2;0;0;0mseveral[39m [38;2;0;0;0mlines[0m
 [38;2;0;0;0monce[39m [38;2;0;0;0mit[39m [38;2;0;0;0mgets[39m [38;2;0;0;0mlong[39m [38;2;0;0;0menough[39m [38;2;0;0;0mto[39m [38;2;0;0;0mexceed[39m [38;2;0;0;0mthe[39m [38;2;0;0;0mviewport[0m
 [38;2;0;0;0mwidth[39m [38;2;0;0;0mthat[39m [38;2;0;0;0mwe[39m [38;2;0;0;0mhand[39m [38;2;0;0;0mto[39m [38;2;0;0;0mthe[39m [38;2;0;0;0mrenderer.[0m

 [38;2;51;51;51m┌──────────────────────────────────────┐[0m
 [38;2;51;51;51m│[0m[3m[38;2;119;119;119mInside[0m [0m[3m[38;2;119;119;119ma[0m [0m[3m[38;2;119;119;119mbordered[0m [0m[3m[38;2;119;119;119mbox.[0m                [38;2;51;51;51m│[0m
 [38;2;51;51;51m│[39m                                      [38;2;51;51;51m│[0m
 [38;2;51;51;51m└──────────────────────────────────────┘[0m

      [38;2;0;0;0m•[39m [38;2;0;0;0mFirst[39m [38;2;0;0;0mitem[0m
      [38;2;0;0;0m•[39m [38;2;0;0;0mSecond[39m [38;2;0;0;0mitem[0m

 [0m[4m[38;2;0;0;255mA link[0m [38;2;0;0;0mand[39m [0m[1m[38;2;0;0;0mbold[0m [38;2;0;0;0mand[39m [38;2;0;0;0mcode.[0m
""",
    "tui-entities": """\

 Entities: & < > "   © —

 Unicode: café, naïve, 日本語テキスト, emoji 😀

 SHOUTING

 Title Case Here
""",
    "tui-images": """\

\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20
 A[img]   B

\x20\x20\x20\x20\x20\x20\x20
 [img]\x20

 ┌────────┐
 [img]    │
 └────────┘
""",
    "tui-links": """\
[0] (7,4 6x2) Page B -> page_b.html
[1] (17,4 6x2) Page C -> page_c.html
[2] (11,6 7x2) example -> https://example.com
[3] (30,6 3x2) top -> #top
""",
    "tui-links-long": """\
[0] (1,4 11x2) Broken link -> missing_file.html
[1] (9,39 17x2) Page B at the end -> page_b.html
""",
    "tui-page-a": """\

 Page A


 Go to Page B or Page C.

 External: example, fragment: top.

\x20\x20\x20\x20\x20\x20\x20\x20\x20
 [img]\x20\x20\x20
""",
    "tui-table": """\
Name  Description\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20
alpha first row of the table\x20\x20\x20

beta  second row

\x20\x20\x20\x20\x20\x20\x20\x20
[img]    after image
""",
}

# ---------------------------------------------------------------------------
# Fixture generation
# ---------------------------------------------------------------------------

def write_png(path, w, h, rgb):
    def chunk(tag, payload):
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    raw = b"".join(b"\x00" + bytes(rgb) * w for _ in range(h))
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b"")
    )


def write_gif(path, w, h, rgb):
    path.write_bytes(
        b"GIF89a"
        + struct.pack("<HH", w, h)
        + bytes([0xF0, 0, 0])
        + bytes(rgb) + b"\x00\x00\x00"
        + b"\x3B"
    )


def write_jpeg_stub(path, w, h):
    """A JPEG carrying a real SOF0 header and nothing else.

    Only used where the header is what matters. Anything that decodes images
    needs a real encoder, which is what the PIL path below provides.
    """
    sof = b"\xff\xc0" + struct.pack(">HBHHB", 17, 8, h, w, 3)
    sof += bytes([1, 0x11, 0, 2, 0x11, 0, 3, 0x11, 0])
    path.write_bytes(b"\xff\xd8" + b"\xff\xe0" + struct.pack(">H", 16)
                     + b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
                     + sof + b"\xff\xd9")


def generate_images(verbose=False):
    """Writes the image fixtures, preferring PIL when it is installed."""
    try:
        from PIL import Image
        have_pil = True
    except ImportError:
        have_pil = False

    for name, (fmt, w, h, rgb) in IMAGES.items():
        target = WORK / name

        if have_pil:
            mode = "P" if fmt == "gif" else "RGB"
            img = Image.new("RGB", (w, h), rgb)
            if mode == "P":
                img = img.convert("P")
            img.save(target)
        elif fmt == "png":
            write_png(target, w, h, rgb)
        elif fmt == "gif":
            write_gif(target, w, h, rgb)
        else:
            write_jpeg_stub(target, w, h)

    return have_pil


def generate_fixtures(verbose=False):
    WORK.mkdir(parents=True, exist_ok=True)

    for name, body in TEXT_FILES.items():
        (WORK / name).write_text(body)

    have_pil = generate_images(verbose)

    if verbose:
        how = "PIL" if have_pil else "built-in writers (PIL not installed)"
        print(f"fixtures in {WORK} ({len(TEXT_FILES)} documents, "
              f"{len(IMAGES)} images via {how})")

    return have_pil


# ---------------------------------------------------------------------------
# Running
# ---------------------------------------------------------------------------

def binary_for(key):
    return BINARY if key == "headless" else TUI_BINARY


def run_case(key, argv):
    proc = subprocess.run([str(binary_for(key))] + argv,
                          capture_output=True, text=True, timeout=60)
    out = proc.stdout
    if proc.returncode != 0:
        out += f"\n[exit {proc.returncode}]\n{proc.stderr}"
    return out


def rewrite_expected(results):
    """Rewrites this file's EXPECTED block in place."""
    source = Path(__file__).read_text()
    start = source.index("EXPECTED = {\n")
    end = source.index("\n}\n\n# ---", start)

    merged = dict(EXPECTED)
    merged.update(results)

    body = []
    for name in sorted(merged):
        text = merged[name].replace("\\", "\\\\")
        lines = []
        for ln in text.split("\n"):
            stripped = ln.rstrip(" ")
            pad = len(ln) - len(stripped)
            lines.append(stripped + "\\x20" * pad if pad else ln)
        body.append('    "%s": """\\\n%s""",' % (name, "\n".join(lines)))

    new = "EXPECTED = {\n" + "\n".join(body) + "\n}\n"
    Path(__file__).write_text(source[:start] + new + source[end + 3:])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--update", action="store_true",
                    help="rewrite the expected blocks in this file")
    ap.add_argument("--no-build", action="store_true", help="skip building first")
    ap.add_argument("--keep", action="store_true",
                    help="leave the scratch directory in place (it is kept anyway)")
    ap.add_argument("-k", metavar="PATTERN", help="only run matching cases")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not args.no_build:
        for target in ("headless", "tui"):
            rc = subprocess.run([sys.executable, str(ROOT / "build.py"),
                                 target]).returncode
            if rc != 0:
                return rc
        print()

    missing = [str(binary_for(k)) for k in ("headless", "tui")
               if not binary_for(k).exists()]
    if missing:
        print("error: not built: " + ", ".join(missing), file=sys.stderr)
        return 1

    generate_fixtures(args.verbose)

    all_cases = cases()
    names = [n for n in all_cases if not args.k or args.k in n]

    if not names:
        print(f"no cases match '{args.k}'", file=sys.stderr)
        return 1

    passed, failed, results = 0, [], {}

    for name in names:
        key, argv = all_cases[name]
        actual = run_case(key, argv)
        results[name] = actual

        if args.update:
            print(f"  rec   {name}")
            continue

        if name not in EXPECTED:
            # Never silently adopt output as the expectation: an empty or
            # broken run would record itself as correct and the case would
            # pass forever after.
            failed.append((name, "<no expected block; run --update>", actual))
            print(f"  NEW   {name}")
        elif actual == EXPECTED[name]:
            passed += 1
            print(f"  ok    {name}")
        else:
            failed.append((name, EXPECTED[name], actual))
            print(f"  FAIL  {name}")

    if args.update:
        rewrite_expected(results)
        print(f"\nrecorded {len(results)} case(s) into {Path(__file__).name}")
        return 0

    print()

    for name, expected, actual in failed:
        print(f"--- {name} ---")
        sys.stdout.writelines(difflib.unified_diff(
            expected.splitlines(keepends=True),
            actual.splitlines(keepends=True),
            fromfile="expected", tofile="actual"))
        print()

    print(f"{passed} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
