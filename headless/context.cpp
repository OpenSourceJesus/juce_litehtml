#include "context.h"

// The master stylesheet is a plain string literal with no JUCE dependency,
// so the headless build reuses the exact same one as the JUCE module rather
// than keeping a second copy in sync.
#include "../juce_litehtml/webengine/master_css.cpp"

namespace headless {

Context::Context()
{
    load_master_stylesheet (juce_litehtml_master_css);
}

Context::~Context() = default;

} // namespace headless
