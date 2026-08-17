#pragma once

#include "litehtml.h"

namespace headless {

/** litehtml context for the headless build.

    Loads the same master stylesheet the JUCE front end uses, and owns the
    quickjs runtime that litehtml::context creates. No JUCE, no GTK.
 */
class Context final : public litehtml::context
{
public:
    Context();
    ~Context() override;
};

} // namespace headless
