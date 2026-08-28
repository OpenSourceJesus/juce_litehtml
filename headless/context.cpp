#include "context.h"

// The master stylesheet is a plain string literal with no JUCE dependency,
// so the headless build reuses the exact same one as the JUCE module rather
// than keeping a second copy in sync.
#include "../juce_litehtml/webengine/master_css.cpp"

#ifdef HEADLESS_WASM
#include "wasm_js.h"
#endif

namespace headless {

Context::Context()
{
    load_master_stylesheet (juce_litehtml_master_css);

#ifdef HEADLESS_WASM
    // WebAssembly modules compiled into this binary ahead of time. Installed
    // on litehtml's own JS context through its public accessor, so no
    // vendored file has to change. Absent entirely from a build that bundles
    // no modules, which is what a page should feature-detect.
    JSContext* jsCtx = js_context();

    if (jsCtx != nullptr)
    {
        JSValue global = JS_GetGlobalObject (jsCtx);
        installWasmBindings (jsCtx, global);
        JS_FreeValue (jsCtx, global);
    }
#endif
}

Context::~Context() = default;

} // namespace headless
