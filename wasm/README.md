# WebAssembly, ahead of time

A `.wasm` in `wasm/modules/` is translated to C at build time by
[Crust](https://github.com/brentharts/crust)'s `tools/wasm2c.py`, compiled with
everything else, and reachable from page JavaScript:

```js
const { instance } = WebAssembly.instantiateBundled("demo");
instance.exports.fib(20);        // 6765
```

```sh
./build.py            # translates and links every module in wasm/modules/
./test_wasm.py        # 12 checks
```

## How it fits together

```
wasm/src/demo.c --crust --target wasm--> wasm/modules/demo.wasm
                --tools/wasm_aot.py-->  build/*/wasm-aot/wasm_demo.c
                                      + build/*/wasm-aot/wasm_demo_glue.cpp
                --build.py------------> linked into the binary
```

`tools/wasm_aot.py` runs the translation and writes the glue that registers a
module's exports with `headless/wasm_registry.cpp`. A module registers itself
from a static initialiser, so dropping a `.wasm` into `wasm/modules/` is the
whole of adding one — there is no list to update.

Translation is skipped when the input has not changed, which matters because a
real module becomes a large C file: `qcms_bg.wasm` translates to about 45,000
lines.

## What this does not do

**It does not run WebAssembly that a page supplies.** A module has to be
present when the browser is built. `WebAssembly.instantiate` and `compile`
exist and always reject, with a message saying so, because a page that
feature-detects deserves a clear answer rather than a missing property.

That limit is deliberate. A translated module is native code with the
process's full privileges, and running downloaded bytes that way gives up the
sandbox that is most of the reason WebAssembly exists. The translated C does
bounds-check every memory access, but `call_indirect` currently checks only
the table bounds and not the signature, so a type-confused indirect call in a
hostile module would not be caught. Running untrusted modules needs either
that check plus a real sandbox, or an interpreter.

So this covers what it is good for: code shipped with the application — a
decoder, a parser, a hot loop — written in whatever compiles to wasm, with no
runtime toolchain, no startup cost, and no temporary files.

## Optional, like everything else

Absent Crust beside the repository, `wasm_aot.py` produces nothing, the build
omits `HEADLESS_WASM`, and there is no `WebAssembly` object at all. The build
still succeeds and `test_wasm.py` skips. This is the same shape as cairo and
TLS.

## Running page scripts

litehtml collects `<script>` text but never ran it: `js_eval` existed with no
caller. `--js` supplies one.

```sh
./build/headless/litehtml-headless --js -m text page.html
```

It is opt-in because running scripts by default would change the output of
every document that has one, and the golden suite depends on that output being
byte-identical. Scripts run after layout, in document order; a script error
goes to stderr and does not stop the ones after it. Re-layout after a script
mutates the DOM is not attempted.
