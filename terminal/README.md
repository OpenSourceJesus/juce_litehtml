# Terminal front ends

Three backends over one renderer. `tui` writes ANSI escape codes and links
nothing; `ncurses` and `notcurses` are interactive viewers. All three share
`TerminalContainer`, `CellGrid` and `TerminalPage` and differ only in how a
grid reaches the screen.

```sh
./build.py tui           # -> build/tui/litehtml-tui
./build.py ncurses       # needs libncursesw5-dev
./build.py notcurses     # needs libnotcurses-dev

./build/tui/litehtml-tui -c 80 doc.html
./build/ncurses/litehtml-ncurses doc.html      # arrows/jk, space/b, g/G, q
```

On Ubuntu: `sudo apt install libncursesw5-dev libnotcurses-dev`.

## How layout reaches the grid

The trick is in `TerminalFont`. litehtml lays out in pixels, so the container
declares one cell to be exactly `cellW` pixels (8 by default) and gives every
glyph an advance of a whole number of cells. Every x coordinate litehtml
produces then divides exactly by `cellW`, and the layout lands on the grid
with nothing to round. Line breaking happens at the right column for free,
because litehtml is measuring in the units the terminal actually has.

Font size is ignored for width -- a terminal cannot draw a 28px heading any
wider than one cell per character -- but honoured for height, rounded up to
whole rows, so headings still get vertical space. CJK and fullwidth code
points advance two cells, and the width table in the synthetic font agrees
with `codepointColumns`, so measurement and display cannot disagree.

This is why the terminal output has none of the word-merging the `ascii` dump
mode suffers from: that mode rounds a proportional layout onto a grid after
the fact, while this one lays out on the grid to begin with.

## Painting

`renderToGrid` runs three passes -- backgrounds, then text, then borders --
rather than following paint order. A terminal cell is far coarser than a CSS
pixel, so a table cell 24px tall is 1.5 rows and its rule and its text land on
the same row. Doing borders last lets a rule see the text already there and
get out of its way, and a box shorter than three rows is dropped entirely: it
has no room for a rule, a row of content and another rule, so drawing it can
only cut through its own text or leave orphaned corners. Dense table borders
are not expressible at one row per line, and the honest result is a readable
table without them.

## What differs between the two interactive backends

Only what the libraries themselves differ on:

| | ncurses | notcurses |
|---|---|---|
| Colour | quantised to the 256-colour cube | RGB directly |
| Inline images | `[img]` placeholder | real pixels |
| Italic | `A_DIM` (A_ITALIC is not in every build) | `NCSTYLE_ITALIC` |

Navigation, scrolling, highlighting and the status line are the same in both,
because they are all `TerminalPage` calls.

## Colour

`Cell` carries fg/bg as packed `0xRRGGBBAA` and the backends quantise as
needed. notcurses takes RGB directly. ncurses cannot, so colours are mapped to
the 6x6x6 cube of the 256-colour palette with a separate grey ramp, and pairs
are allocated on demand. That difference is the main reason to prefer
notcurses if you have the choice.

## Testing

The ANSI backend is in the golden suite (`tui-*` cases in `run_tests.py`),
covering reflow at two widths, tables, unicode and colour escapes. It stands
in for the other two: they share every line above the blit, and neither can
run over a pipe.

`ncurses` and `notcurses` were verified by driving them under a pseudo
terminal. notcurses probes the terminal for capabilities at startup and will
appear to hang if nothing answers, which is a property of the harness rather
than a bug.

## Links and navigation

Both interactive backends are browsers rather than viewers: `tab` / `n` / `p`
select a link, `enter` follows it, `backspace` goes back. The status line
shows the target of the focused link, and focusing a link off screen scrolls
to it. Focus wraps at either end of the document.

Links come from `collectLinks` (headless/links.h), which walks the tree after
layout. An inline `<a>` has a degenerate placement of its own -- litehtml
keeps the geometry on the text elements inside it, so asking the anchor where
it is gives a 1x1 box at the line start -- so the box is the union of
everything beneath it. The highlight then paints only cells that hold a
glyph, because that union takes in the line's leading and would otherwise
reverse a blank row under the words.

Only local relative links are followed. A fragment, an external URL or a
`mailto:` is reported on the status line rather than attempted: there is no
network loader here, and inventing one silently would be worse than saying so.

Anything that cannot be followed says so on the status line rather than
failing quietly -- a missing file, an external URL, a fragment, `enter` with
nothing selected, `backspace` with no history.

## Images

`get_image_size` reads dimensions straight out of the file header --
`headless/imageinfo.h` handles PNG, GIF, JPEG and BMP. Layout needs a size and
nothing else, so no decoding happens and the base container stays free of any
image library. Every front end gets correct image layout from this, including
the ANSI one that cannot draw pixels.

notcurses then draws the pixels. `ncvisual_options` carries no destination
size -- the target size is the plane's -- so each image gets a child plane of
exactly the cell box layout reserved, and the image is stretched into it.
Images scrolled partly off screen are skipped rather than clipped: a plane
cropped at the top would need its own crop rectangle, and half an image is
worse than the space it would have occupied.

Backends that cannot draw pixels show an `[img]` placeholder instead, sized to
the same box; ncurses uses it, and notcurses turns it off
(`setImagePlaceholders(0)`) so it is not drawing over its own images.

Note `ncvisual_from_file` returns NULL if called before `notcurses_init`,
which is not obvious from its signature.

## Known gaps

- No forward history, only back.
- No fragment navigation: `#anchor` is refused rather than scrolled to, though
  the element is in the tree and could be found.
- No network loading, so `http://` links cannot be followed.
- Animated GIFs render their first frame; notcurses can play them.
- No search, no text selection.
