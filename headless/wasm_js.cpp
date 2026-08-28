#include "wasm_js.h"
#include "wasm_registry.h"

namespace headless {

/*  A call into a bundled module.

    The module and the export are carried on the function object itself
    rather than looked up by name on every call: quickjs gives each C
    function a small integer `magic` and an opaque data array, and putting
    the pointers there means a call is a cast and a jump.
 */
static JSValue callExport (JSContext* ctx, JSValueConst thisVal,
                    int argc, JSValueConst* argv,
                    int magic, JSValue* data)
{
    (void) thisVal;
    (void) magic;

    int32_t modIndex = 0;
    int32_t expIndex = 0;
    JS_ToInt32 (ctx, &modIndex, data[0]);
    JS_ToInt32 (ctx, &expIndex, data[1]);

    const WasmModuleDef* mod = wasmModuleAt (modIndex);

    if (mod == nullptr || expIndex < 0 || expIndex >= mod->exportCount)
        return JS_ThrowInternalError (ctx, "wasm export no longer available");

    const WasmExport* fn = &mod->exports[expIndex];

    double args[4];
    int n = 0;

    while (n < argc && n < 4)
    {
        double v = 0.0;
        JS_ToFloat64 (ctx, &v, argv[n]);
        args[n] = v;
        n++;
    }

    bool ok = false;
    const double result = wasmCallExport (mod, fn, args, n, &ok);

    if (! ok)
        return JS_ThrowTypeError (ctx,
                                  "wasm export '%s' takes %d argument(s) of "
                                  "a shape this build can call",
                                  fn->name, (int) 0);

    if (fn->result == 'v')
        return JS_UNDEFINED;

    return JS_NewFloat64 (ctx, result);
}

/** `WebAssembly.instantiateBundled(name)` -> { instance: { exports } }.

    Shaped like the real `instantiate` result so that page code can be written
    against the standard API and only the call that obtains the module has to
    differ.
 */
static JSValue instantiateBundled (JSContext* ctx, JSValueConst thisVal,
                            int argc, JSValueConst* argv)
{
    (void) thisVal;

    if (argc < 1)
        return JS_ThrowTypeError (ctx, "instantiateBundled needs a name");

    const char* name = JS_ToCString (ctx, argv[0]);

    if (name == nullptr)
        return JS_EXCEPTION;

    int modIndex = -1;

    const int total = wasmModuleCount();

    for (int i = 0; i < total; ++i)
    {
        const WasmModuleDef* m = wasmModuleAt (i);

        if (m != nullptr && wasmFindModule (name) == m)
        {
            modIndex = i;
            break;
        }
    }

    if (modIndex < 0)
    {
        JSValue err = JS_ThrowTypeError (
            ctx, "no bundled wasm module named '%s'", name);
        JS_FreeCString (ctx, name);
        return err;
    }

    JS_FreeCString (ctx, name);

    const WasmModuleDef* mod = wasmModuleAt (modIndex);
    wasmEnsureInit (mod);

    JSValue exports = JS_NewObject (ctx);

    for (int i = 0; i < mod->exportCount; ++i)
    {
        JSValue data[2];
        data[0] = JS_NewInt32 (ctx, modIndex);
        data[1] = JS_NewInt32 (ctx, i);

        JSValue fn = JS_NewCFunctionData (ctx, callExport, 0, 0, 2, data);

        JS_FreeValue (ctx, data[0]);
        JS_FreeValue (ctx, data[1]);

        JS_SetPropertyStr (ctx, exports, mod->exports[i].name, fn);
    }

    JSValue instance = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, instance, "exports", exports);

    JSValue result = JS_NewObject (ctx);
    JS_SetPropertyStr (ctx, result, "instance", instance);
    return result;
}

/** `WebAssembly.instantiate` / `compile`: present, and always rejecting.

    A page that feature-detects gets a clear answer instead of finding the
    property missing and guessing; a page that calls anyway fails here with a
    message saying what this build does support, rather than further along
    with something unhelpful.
 */
static JSValue unsupported (JSContext* ctx, JSValueConst thisVal,
                     int argc, JSValueConst* argv)
{
    (void) thisVal;
    (void) argc;
    (void) argv;
    return JS_ThrowTypeError (
        ctx,
        "this build runs only WebAssembly modules compiled into it; "
        "use WebAssembly.instantiateBundled(name), and see "
        "WebAssembly.bundled for what is available");
}

void installWasmBindings (JSContext* ctx, JSValue global)
{
    JSValue wasm = JS_NewObject (ctx);

    JS_SetPropertyStr (ctx, wasm, "instantiateBundled",
                       JS_NewCFunction (ctx, instantiateBundled,
                                        "instantiateBundled", 1));
    JS_SetPropertyStr (ctx, wasm, "instantiate",
                       JS_NewCFunction (ctx, unsupported, "instantiate", 2));
    JS_SetPropertyStr (ctx, wasm, "compile",
                       JS_NewCFunction (ctx, unsupported, "compile", 1));

    // The names of the modules this binary carries, so a page can find out
    // what it has rather than guessing.
    JSValue names = JS_NewArray (ctx);

    const int total = wasmModuleCount();

    for (int i = 0; i < total; ++i)
    {
        const WasmModuleDef* m = wasmModuleAt (i);

        if (m != nullptr)
            JS_SetPropertyUint32 (ctx, names, (uint32_t) i,
                                  JS_NewString (ctx, m->name));
    }

    JS_SetPropertyStr (ctx, wasm, "bundled", names);
    JS_SetPropertyStr (ctx, global, "WebAssembly", wasm);
}

} // namespace headless
