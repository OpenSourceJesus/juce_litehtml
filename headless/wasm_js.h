#pragma once

#include "quickjs.h"

namespace headless {

/** Installs the `WebAssembly` object on a quickjs global.

    What a page sees is deliberately a *subset* of the browser API, because
    the modules are compiled ahead of time (see wasm_registry.h):

        WebAssembly.bundled                 -> ["demo", ...]
        WebAssembly.instantiateBundled(n)   -> { instance: { exports: {...} } }

    `WebAssembly.instantiate` and `compile` exist and always reject, with a
    message saying why. That is better than leaving them undefined: a page
    that feature-detects gets a clear answer, and one that tries anyway fails
    at the call rather than somewhere later with `undefined is not a
    function`.

    The exports of a bundled module are ordinary JavaScript functions, so
    calling one is a direct call into linked-in code -- no compilation, no
    temporary files, nothing written while a page is open.
 */
void installWasmBindings (JSContext* ctx, JSValue global);

} // namespace headless
