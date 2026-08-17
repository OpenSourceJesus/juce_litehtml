#pragma once

#include "crust_compat.h"
#include "litehtml.h"

namespace headless {

/** One anchor in the laid-out document. */
class Link
{
public:
    std::string href;
    std::string text;
    int x;
    int y;
    int width;
    int height;

    Link();
};

/** Collects every <a href> in the document with its rendered box.

    litehtml reports a click through on_anchor_click, but a keyboard-driven
    front end has to know where the links *are* before anything is clicked,
    so this walks the tree after layout.

    An inline anchor spanning several lines still has one box, which is enough
    to focus and follow it.
 */
void collectLinks (litehtml::document::ptr doc, std::ownvector<Link>* out);

/** Resolves a possibly-relative href against a base directory, producing a
    local path. Returns 0 for an absolute URL or a fragment, which a local
    front end cannot follow.
 */
int resolveHref (const char* baseDir, const char* href, std::string* out);

} // namespace headless
