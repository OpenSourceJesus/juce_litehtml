#pragma once

#include "litehtml.h"

namespace headless {

/** A <script> element that can actually read its own attributes.

    litehtml's built-in el_script derives from litehtml::element, whose
    get_attr() is a stub returning the default value. As a result the src
    attribute is never found and import_script() is never called. Deriving
    from html_tag instead gives us the real attribute map. The JUCE module
    works around the same bug in the same way.
 */
class ScriptElement final : public litehtml::html_tag
{
public:
    explicit ScriptElement (const litehtml::document::ptr& doc);

    void parse_attributes() override;
    void set_data (const litehtml::tchar_t* data) override;

    // Pointer, not a reference: the subset refuses a reference return.
    litehtml::tstring* getScript() { return &script; }

private:
    litehtml::tstring script;
};

} // namespace headless
