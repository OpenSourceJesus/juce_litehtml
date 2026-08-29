# litehtml, most of a browser

This began as a JUCE module wrapping the [litehtml](https://github.com/litehtml/litehtml)
HTML/CSS engine. It is now also a small browser that runs without JUCE, without
a window system, and — where it matters — without any third-party library at
all.

The engine is unchanged: litehtml does layout, [quickjs-ng](https://github.com/quickjs-ng/quickjs)
(v0.16.x, vendored under `juce_litehtml/quickjs/`) runs scripts, and JUCE paints.
is there for scripting. What is new is everything around it, written so it can
be lowered to C by [Crust](https://github.com/brentharts/crust).

```sh
./build.py                                          # headless renderer + tests
./build.py tui   && ./build/tui/litehtml-tui doc.html
./build.py ncurses && ./build/ncurses/litehtml-ncurses --net https://example.com/
```

## What exists

| Target | What it is | Needs |
|---|---|---|
| `headless` | Renderer with synthetic fonts; text/tree/display-list dumps | nothing |
| `cairo` | Real font metrics, renders to PNG | cairo |
| `tui` | Terminal rendering via ANSI escape codes | nothing |
| `ncurses` | Interactive terminal browser | libncursesw |
| `notcurses` | Interactive terminal browser with inline images | libnotcurses |
| `gtk` | Windowed browser: links, history, decoded images | gtk3 |
| `mininodejs` | Node-like JavaScript runner over quickjs | nothing |

WebAssembly modules in `wasm/modules/` are translated to C at build time by
Crust and linked in; see [wasm/README.md](wasm/README.md). Absent Crust the
build simply omits them.

All of them share one container hierarchy and one loader. A front end overrides
the font metrics and the paint calls; everything else — image sizing,
stylesheet and script loading, media features, link collection — is inherited.

Both terminal browsers do links (`tab` to select, `enter` to follow),
back-history (`backspace`), scrolling, and documents over http/https.

```
 Remote Index
 Served over HTTP with an external stylesheet.
 ┌────────┐
 │ [img]  │  inline image
 └────────┘
 Links: deep page and root-relative.
─────────────────────────────────────────────
 Remote Index  -> sub/deep.html
```

## Building

`build.py` replaces make and cmake for everything except the JUCE module. It
compiles each translation unit, tracks header dependencies through gcc's
`-MMD`, and rebuilds only what changed — the compile command itself is part of
the cache key, so changing a flag forces a rebuild.

```sh
./build.py --list          # targets, and whether their deps are present
./build.py cairo --release
./build.py --clean tui
./build.py -j 8
```

On Ubuntu, for the optional targets:

```sh
sudo apt install libcairo2-dev libncursesw5-dev libnotcurses-dev libssl-dev
```

None of them are required. TLS in particular is probed for: with openssl
present the loader compiles with `-DHEADLESS_TLS` and https works; without it
everything still builds and https reports that it is unsupported rather than
failing to link.

## Testing

```sh
./run_tests.py        # 22 golden cases, entirely offline
./test_network.py     # 10 checks against a server it starts itself
./test_gtk.py         # 10 checks on the GTK front end, no display needed
./test_mininodejs.py  # JS runner tests, plus an ES feature matrix
./check_crust.py      # scans our own sources for constructs Crust refuses
./test_wasm.py        # 12 checks on the ahead-of-time WebAssembly path
```

All of them are self-contained. `run_tests.py` embeds both the documents under
test and their expected output, writing fixtures to `/tmp/litehtml-tests` as
it runs; `test_network.py` starts its own loopback server. So there are no
fixture directories to keep in sync, and no suite depends on the outside
world.

The golden suite works because **output is deterministic**. The headless
container computes text metrics from a fixed width table rather than from
system fonts, so the same input produces byte-identical output on any machine
at any optimisation level. That property has already paid for itself twice: it
turned a from-scratch rewrite of every file into a verifiable change, and it
catches layout regressions that would otherwise need a human to eyeball.

Write assertions against `text` and `tree`. The `ascii` dump is a coarse grid
meant for eyeballing structure, not for comparison.

---

# The C++ subset

Everything under `headless/`, `terminal/` and `cairo/` is written in the
**Crust C++ subset** (`CPPRUST.md`), which `tools/cpprust.py` lowers to C. The
same sources also build with an ordinary C++ compiler, and that dual
requirement is what most of the style below exists to satisfy.

The guiding rule from the Crust side is worth repeating, because it explains
why the constraints are hard rather than advisory:

> **Anything the lowering cannot do correctly is reported, not approximated.**

A refusal is the contract. Code that leans on something outside the subset does
not degrade gracefully — it stops translating.

## Rules to follow

**No standard library beyond what Crust supplies.** That is `string`,
`vector<T>`, `ownvector<T>`, `map`, `unique_ptr`, `shared_ptr`, and nothing
else. In particular:

- No `<sstream>`, `<fstream>`, or iostreams. Build text by appending to a
  `std::string` (`headless/strbuf.h`); do file IO with `fopen`/`fread`.
- No `<algorithm>`. Write the loop. The sort in `dump.cpp` is a hand-written
  insertion sort for exactly this reason.
- No `std::max`, `std::min`, `std::function`, `std::pair`, `std::optional`.

**Owning elements go in `ownvector<T>`, not `vector<T>`.** `vector` stores by
assignment, which would leave two owners of one resource. `ownvector`
copy-constructs and destroys each element. `crust_compat.h` aliases it to
`vector` for a normal C++ build, behind an `#ifndef CRUST` the lowering
evaluates away.

**No reference returns.** Return a pointer. Lowering `T&` to `T*` would
silently change what assignment through the result means at every call site, so
it is refused — `operator[]` and `operator*` are the only exceptions.

**No default function arguments.** Spell every argument at every call.

**No capturing lambdas in a condition.** They are inlined at the call site, so
they cannot appear in a loop condition, in a `&&`/`||` operand, or in a
ternary, where the body would not run exactly once. The safest habit is to
avoid lambdas here entirely; this codebase has none.

**No `enum class`, `constexpr`, `emplace_back`, `vector<bool>`, exceptions,
`dynamic_cast`, `typeid`, `goto`, array `new`/`delete[]`, multiple
inheritance.**

**Range-`for` only over a nameable chain.** `for (auto& x : v)` is fine;
`for (auto& x : getThings())` is not, because the lowering has to read a length
from something it can name. Index loops are the default here.

**Single inheritance only, and it is used deliberately.** `Container` derives
from `litehtml::document_container`; `CairoContainer` and `TerminalContainer`
derive from `Container`. That chain is fine. A second base is not.

**Include order matters when a C library has macros.** `curses.h` defines
`border`, `line` and `box` as macros and litehtml has a class called `border`,
so litehtml headers come first. This is a plain C++ problem, not a Crust one,
but it is the kind of thing that costs an hour.

`./check_crust.py` enforces the mechanical parts of this. It is a text scan,
not a parser — the same position `cpprust.py` is in — so it catches known-bad
constructs early but cannot prove a file lowers.

## Things inherited from the vendored engine

Two are worth knowing before touching litehtml:

- **litehtml here depends on quickjs.** `litehtml/include/context.h` includes
  `quickjs.h`, so even the headless build links the JS engine. This path is
  free of JUCE and GTK, not free of dependencies.
- **litehtml's `el_script` cannot read its own attributes.** It derives from
  `litehtml::element`, whose `get_attr()` is a stub returning the default, so
  `src` is never found and `import_script()` never fires. Both this project and
  the JUCE module work around it by supplying a replacement derived from
  `html_tag`. If an element type mysteriously ignores its attributes, check
  what it derives from first.

**The fork's `var()` support has been patched.** `style::subst_vars` had three
defects: no support for the fallback form `var(--x, blue)`, no handling of
parentheses inside a fallback, and -- the damaging one -- it left the literal
text `var(--x)` in place when a variable was undefined. That text then reached
the colour parser, and `web_color::from_string` returns opaque black for
anything it cannot parse, so a page setting both background and text through
variables rendered black on black. An unresolvable `var()` now drops the whole
declaration, which is what CSS requires. This is the one change to vendored
code beyond build flags; it is in `litehtml/src/style.cpp` and
`litehtml/include/litehtml/style.h`.

GCC 13 also rejects `litehtml/include/element.h` under `-Wchanges-meaning`,
where a member shadows its own class name. `build.py` suppresses the warning
rather than patching vendored code; revisit if this is ever rebased onto
upstream litehtml.

---

## How the pieces fit

```
litehtml + quickjs            (vendored, unchanged)
        │
headless/container.*          document_container: records a display list
   ├─ url.* loader.*          URLs, http/https/file, caching
   ├─ imageinfo.*             image dimensions from file headers
   ├─ links.*                 anchors and their boxes, after layout
   └─ dump.*                  text / tree / display-list / ascii output
        │
        ├─ cairo/             real fonts, PNG output
        │     └─ gtk/         window, events, decoded images
        └─ terminal/          cell-aligned metrics, one grid
              ├─ ansi         escape codes, no dependencies
              ├─ ncurses      interactive
              └─ notcurses    interactive, inline images
```

Two design decisions carry most of the weight.

**The display list.** The base container never rasterises; it records paint
calls. A front end that draws for real still calls the base first, so the dump
modes and the golden tests keep working on a container that also draws. This is
why a headless test can assert on what a GTK window would have shown.

**Cell-aligned metrics.** litehtml lays out in pixels. The terminal container
declares one cell to be exactly 8 pixels and gives every glyph a whole-cell
advance, so every x coordinate divides exactly by the cell width and the layout
lands on the grid with nothing to round. Line breaking then happens at the
right column for free. Compare the `ascii` dump mode, which rounds a
proportional layout onto a grid afterwards and merges words together.

Details are in [gtk/README.md](gtk/README.md),
[headless/README.md](headless/README.md),
[headless/NETWORK.md](headless/NETWORK.md) and
[terminal/README.md](terminal/README.md).

## Where this is going

Nearest first:

- **Charset handling.** Everything is assumed UTF-8. A page served as latin-1
  renders wrongly, and much of the older web is latin-1. This is the difference
  between browsing the web and browsing the subset of it that agrees with us.
- **Fragment navigation.** `#anchor` is refused, though the element is in the
  tree with a known y position. Both terminal browsers would get it at once.
- **Forward history**, connection reuse, `Cache-Control`.

**JavaScript.** `mininodejs` runs snippets against the same engine the browser
links, without a page in the way (see [mininode/README.md](mininode/README.md)).
The vendored engine is [quickjs-ng](https://github.com/quickjs-ng/quickjs) 0.16.x
(replacing the March 2021 Bellard snapshot), so static class blocks, private
fields, BigInt, and top-level await (as an ES module, `mininodejs -m`) work
out of the box.

Further out: form controls, text selection, and letting quickjs actually run
the scripts the loader is already fetching.

---

# The JUCE module

The original module is unchanged and still builds through cmake. Nothing above
touches it.

## Compilation

This module must be used via CMake (cannot be used from Projuces because of additional targets that need to be compiled).

Add [JUCE](https://github.com/juce-framework/JUCE) and [juce_litehtml](https://github.com/Archie3d/juce_litehtml) submodules to your project, and then link additional `juce::juce_litehtml` library on your target:

```CMake
add_subdirectory(JUCE)
add_subdirectory(juce_litehtml)

target_link_libraries(${TARGET}
    PRIVATE
        juce::juce_core
        juce::juce_data_structures
        juce::juce_gui_basics

        juce::juce_litehtml

        juce::juce_recommended_config_flags
)
```

[See the test project](https://github.com/Archie3d/juce_litehtml_test) for an example.
