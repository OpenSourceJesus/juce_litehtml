#include "el_script.h"

#include <string.h>

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


namespace headless {

/*  Walking the element tree.

    litehtml has no "find all elements of this type" call, so the tree is
    walked by hand. select_all() with a "script" selector would be shorter,
    but it returns elements in selector-match order rather than document
    order, and scripts must run in the order they appear.
 */
static void collectScripts (const litehtml::element::ptr& el,
                            std::vector<ScriptElement*>* out)
{
    if (! el)
        return;

    // Identified by tag name rather than dynamic_cast: the subset has no
    // RTTI, and Container::create_element makes a ScriptElement for exactly
    // this tag, so the name is as reliable a test as the type would be.
    const litehtml::tchar_t* tag = el->get_tagName();

    if (tag != nullptr && strcmp (tag, "script") == 0)
        out->push_back (static_cast<ScriptElement*> (el.get()));

    // m_children is protected, so the tree is walked through the public
    // count/index pair rather than by iterating the vector directly.
    const size_t n = el->get_children_count();

    for (size_t i = 0; i < n; ++i)
        collectScripts (el->get_child ((int) i), out);
}

void runDocumentScripts (const litehtml::document::ptr& doc,
                         litehtml::context* ctx)
{
    if (! doc || ctx == nullptr)
        return;

    std::vector<ScriptElement*> scripts;
    collectScripts (doc->root(), &scripts);

    const size_t count = scripts.size();

    for (size_t i = 0; i < count; ++i)
    {
        litehtml::tstring* stored = scripts[i]->getScript();
        litehtml::tstring source;

        if (stored != nullptr)
            source = *stored;

        if (source.empty())
        {
            // An inline script's body does not always arrive through
            // set_data: litehtml's parser can put it in a text child
            // instead, in which case getScript() is empty and the code is
            // sitting in the element's text. Reading that is the difference
            // between running inline scripts and silently ignoring them.
            scripts[i]->get_text (source);
        }

        if (source.empty())
            continue;

        JSValue result = ctx->js_eval (source);

        if (JS_IsException (result))
        {
            JSContext* jsCtx = ctx->js_context();
            JSValue err = JS_GetException (jsCtx);
            const char* msg = JS_ToCString (jsCtx, err);

            fprintf (stderr, "script error: %s\n",
                     msg != nullptr ? msg : "(unknown)");

            if (msg != nullptr)
                JS_FreeCString (jsCtx, msg);

            JS_FreeValue (jsCtx, err);
        }

        JS_FreeValue (ctx->js_context(), result);
    }
}

} // namespace headless
