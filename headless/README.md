# Headless litehtml

A rendering path with no JUCE, no GTK, and no window system. It exists so the
HTML/CSS engine can be exercised and regression-tested from a terminal or CI
job, and so the GTK front end has a known-good reference to compare against.

## Building

```sh
./build.py                # build the headless target (default)
./build.py --release      # optimised
./build.py --clean        # remove this target's artefacts
./build.py --list         # show targets
./build.py -j 8           # limit parallelism
```

There is no make or cmake in this path. `build.py` compiles each translation
unit, records header dependencies through gcc's `-MMD`, and rebuilds only what
changed. The compile command itself is part of the cache key, so changing a
flag correctly forces a rebuild.

The binary lands at `build/headless/litehtml-headless`.

## Running

```sh
litehtml-headless [options] [file.html]
```

Reads from stdin when no file is given. Modes:

| Mode    | Output |
|---------|--------|
| `text`  | reconstructed page text, one line per rendered line box |
| `tree`  | laid-out element tree with box geometry |
| `draw`  | recorded display list in paint order |
| `ascii` | coarse ASCII rendering |
| `stats` | summary counters |
| `all`   | all of the above |

```sh
./build/headless/litehtml-headless -w 400 -m text headless/testdata/basic.html
echo '<p>hi</p>' | ./build/headless/litehtml-headless -m tree
```

Options: `-w/--width`, `-h/--height`, `-m/--mode`, `-b/--base`, `--font-name`,
`--font-size`, `--image-size WxH`.

When a file is given, `--base` defaults to that file's directory, so relative
`<link rel=stylesheet>` and `<script src>` urls resolve without extra flags.
Only local paths are resolved; network loading belongs to the front ends.

## Tests

```sh
./run_tests.py            # build, then compare against golden files
./run_tests.py --update   # re-record the golden files
./run_tests.py -k table   # run matching cases only
```

Golden files live in `headless/expected/`. This works because output is
deterministic: the container computes text metrics from a fixed width table
rather than from system fonts, so the same input produces byte-identical
output on any machine, at any optimisation level.

Write assertions against `text` and `tree`. `ascii` is a coarse 6x12 grid and
is meant for eyeballing block structure, not for precise comparison.

## The C++ subset

These sources are written to the **Crust C++ subset** (CPPRUST.md) so they can
be lowered to C by `tools/cpprust.py`, and they also build with an ordinary
C++ compiler. In practice that means:

- no `<sstream>`, `<fstream>` or iostreams -- text is appended to a
  `std::string` (`strbuf.h`), and file IO is `fopen`/`fread`
- no `<algorithm>` -- loops are written out, including the sort in `dump.cpp`
- no `enum class`, `constexpr`, `emplace_back`, `std::map`, `vector<bool>`
- no reference returns, no default arguments, no anonymous namespaces
- range-`for` only over a nameable chain, never over a call result
- capturing lambdas avoided entirely: they are inlined at each call site, so
  they cannot appear in a loop condition, which is where they were being used

Element types that own something go in `std::ownvector<T>` rather than
`std::vector<T>`, because `vector` stores by assignment and would leave two
owners. `ownvector` is Crust's; `crust_compat.h` aliases it to `vector` for a
normal C++ build, behind an `#ifndef CRUST` the lowering evaluates away.

```sh
./check_crust.py            # scan our own sources for refused constructs
```

That linter is a text scan, not a parser -- the same position `cpprust.py` is
in. It catches known-refused constructs early; it cannot prove a file lowers.

## Design

`Container` implements `litehtml::document_container`. Nothing is rasterised;
paint calls are recorded into a display list which the dump functions then
interpret. Fonts are synthetic: `Font::textWidth` sums per-codepoint advances
from a Helvetica-like table, widening for bold and treating CJK ranges as
full-width.

`Context` subclasses `litehtml::context` and `#include`s the JUCE module's
`master_css.cpp` directly rather than keeping a second copy of the master
stylesheet in sync.

## Front ends

`Container` is the base. A front end that draws for real subclasses it and
overrides only the font and paint methods; image sizing, css and script
loading, media features, `transform_text` and `create_element` are inherited
unchanged. `Font` is virtual for the same reason, so a subclass can carry a
real platform font handle.

`cairo/` is the worked example: `CairoFont` overrides the two metric methods,
`CairoContainer` overrides the four paint calls, and each paint call still
chains to the base so the display list -- and therefore the dump modes and the
tests -- keeps working on a container that also draws.

```sh
./build.py cairo
./build/cairo/litehtml-cairo -w 500 -o page.png doc.html
```

Cairo needs no window system, so this runs in CI. GTK sits on top of exactly
this container and supplies a different `cairo_t`.

Note that cairo output will **not** match the headless golden files, and
should not: Cairo measures real fonts, so text wraps differently. The goldens
pin the synthetic path only.

## Notes on the vendored engine

Two things worth knowing, both inherited rather than introduced here:

- **litehtml in this repo depends on quickjs.** `litehtml/include/context.h`
  includes `quickjs.h`, so even the headless build links the JS engine. This
  path is free of JUCE and GTK, not free of dependencies.

- **litehtml's built-in `el_script` cannot read its own attributes.** It
  derives from `litehtml::element`, whose `get_attr()` is a stub returning the
  default, so `src` is never found and `import_script()` never fires. The
  headless build supplies its own `ScriptElement` (derived from `html_tag`)
  through `Container::create_element`. The JUCE module works around the same
  bug the same way. Scripts are recorded, not executed.

GCC 13 rejects `litehtml/include/element.h` under `-Wchanges-meaning`, where a
member shadows its own class name. `build.py` suppresses the warning rather
than patching vendored code; revisit if this is ever rebased onto upstream
litehtml.
