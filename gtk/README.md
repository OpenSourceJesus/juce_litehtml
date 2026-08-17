# GTK front end

A window, a scroll position and an event loop. Everything below that is
inherited: `GtkContainer` derives from `CairoContainer`, which derives from
the headless `Container`, so layout, loading, fonts and painting already
existed before this directory did. GTK supplies a different `cairo_t` and
nothing about the drawing changes.

```sh
./build.py gtk
./build/gtk/litehtml-gtk page.html
./build/gtk/litehtml-gtk --net https://example.com/
```

On Ubuntu: `sudo apt install libgtk-3-dev`.

Click a link to follow it. Backspace or alt+left goes back, ctrl+l focuses the
address bar, ctrl+q quits. The address bar accepts a path or a URL.

## What this adds over the cairo container

**Images.** Cairo can only read PNG; gdk-pixbuf decodes everything GTK ships a
loader for. The bytes come from the base container's loader, so this shares
the cache with the sizing pass — a remote image costs one request, not two —
and a remote image works exactly like a local one. Decoded pixbufs are cached,
including failures, so a broken image is not re-fetched and re-decoded on
every redraw.

**Cursor feedback.** litehtml reports `pointer` through `set_cursor` when the
mouse is over a link, and the window turns that into a hand.

## Testing without a display

```sh
./test_gtk.py
```

Two options exist for this, and they are the reason the GTK path is checkable
in CI at all:

- `--screenshot FILE` lays out and renders to a png offscreen. No display, no
  window, no X server.
- `--click X,Y` follows the link at a document coordinate before rendering,
  which is what lets navigation be tested without synthesising X events.

The suite checks rendering, that all three image decoders run (a red png, a
blue jpeg and a green gif — each colour present means that decoder worked),
that clicking follows the right link, that a click hitting nothing is
reported, and that the network stays off unless asked.

What it does not check is the window itself: event plumbing, scrolling and the
cursor change still want a human, or an X server and a screenshot. Both were
verified by hand under Xvfb during development.

## Known gaps

- Text is drawn through cairo's toy font API, inherited from `CairoContainer`.
  Pango would do better with complex scripts, ligatures and fallback fonts.
- No selection, no find, no zoom.
- No forward history, only back.
- Fragment links (`#anchor`) are refused rather than scrolled to.
- Reflow happens on every width change, which reparses the document; it should
  re-render rather than re-create.
- Scroll position is not preserved across navigation, and going back returns
  to the top of the page rather than where you left it.
