#pragma once

/** Registry of ahead-of-time compiled WebAssembly modules.

    A `.wasm` bundled with the browser is translated to C at build time by
    Crust's `tools/wasm2c.py` and compiled in with everything else. Nothing
    is translated, compiled or loaded while a page is being rendered: by the
    time the browser runs, a module is ordinary linked-in code.

    That is the whole design, and it is worth being explicit about what it
    does *not* do. It does not run WebAssembly that a page supplies. A module
    has to be present when the browser is built, so this covers bundled and
    trusted code -- a decoder shipped with the application, say -- and not
    arbitrary bytes fetched over the network. Running those safely needs an
    interpreter or a sandbox, because a translated module is native code with
    the process's full privileges.

    Each module registers itself from a static initialiser in its generated
    glue, so adding one to the build is enough to make it visible here; there
    is no central list to keep in step.
 */

namespace headless {

/** One exported function of a module.

    The signature is described rather than typed, because the registry has to
    hold functions of many shapes in one table. `call` unpacks the arguments
    according to `sig` -- see wasm_registry.cpp.
 */
struct WasmExport
{
    const char* name;
    /** Parameter kinds, one character each: 'i' i32, 'j' i64, 'f' f32,
        'd' f64. Empty for no parameters. */
    const char* params;
    /** Result kind: the same characters, or 'v' for no result. */
    char result;
    /** The generated `w2c_export_*` function, cast to a common type. The
        caller casts it back using `params` and `result`. */
    void* fn;
};

/** One AOT-compiled module. */
struct WasmModuleDef
{
    const char* name;
    /** Generated `wasm_init`: lays out memory and fills the tables. Called
        once, lazily, the first time the module is used. */
    void (*init)();
    const WasmExport* exports;
    int exportCount;
};

/** Registers a module. Called from a static initialiser in generated glue;
    not meant to be called by hand. */
void wasmRegisterModule (const WasmModuleDef* def);

/** Number of registered modules, and the i'th one. */
int wasmModuleCount();
const WasmModuleDef* wasmModuleAt (int index);

/** The module with this name, or null. */
const WasmModuleDef* wasmFindModule (const char* name);

/** The named export of a module, or null. */
const WasmExport* wasmFindExport (const WasmModuleDef* mod, const char* name);

/** Runs a module's one-time initialisation if it has not run yet.

    Lazy rather than eager because a module's memory can be megabytes, and a
    browser that bundles several should not pay for the ones a page never
    touches.
 */
void wasmEnsureInit (const WasmModuleDef* mod);

/** Calls an export.

    Arguments and the result are carried as doubles, which is what the
    JavaScript side has anyway. i64 is the one lossy case and is rejected by
    the caller rather than silently truncated here.
 */
double wasmCallExport (const WasmModuleDef* mod, const WasmExport* fn,
                       const double* args, int argCount, bool* ok);

} // namespace headless
