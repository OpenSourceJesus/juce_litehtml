#include "el_script.h"

namespace headless {

ScriptElement::ScriptElement (const litehtml::document::ptr& doc)
    : litehtml::html_tag (doc)
{
}

void ScriptElement::parse_attributes()
{
    auto doc = get_document();

    if (! doc)
        return;

    if (const litehtml::tchar_t* src = get_attr (_t ("src")))
    {
        // The container records the request and fills in the source if it
        // can resolve the url locally.
        litehtml::tstring loaded;
        doc->container()->import_script (loaded, src);
        script = loaded;
    }
}

void ScriptElement::set_data (const litehtml::tchar_t* data)
{
    // Inline script body.
    if (data != nullptr)
        script += data;
}

} // namespace headless
