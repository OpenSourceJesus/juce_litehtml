#include "links.h"

#include <string.h>

namespace headless {

Link::Link()
{
    x = 0;
    y = 0;
    width = 0;
    height = 0;
}

/** Unions the boxes of an element and everything under it.

    An inline <a> has a degenerate placement of its own -- litehtml keeps the
    geometry on the text elements inside it -- so asking the anchor where it
    is gives a 1x1 box at the line start. The real extent is the union of what
    is beneath it.
 */
static void unionBox (litehtml::element::ptr el, int* found,
                      int* x0, int* y0, int* x1, int* y1)
{
    if (! el)
        return;

    litehtml::position pos = el->get_placement();

    if (pos.width > 0 && pos.height > 0)
    {
        if (*found == 0)
        {
            *x0 = pos.x;
            *y0 = pos.y;
            *x1 = pos.x + pos.width;
            *y1 = pos.y + pos.height;
            *found = 1;
        }
        else
        {
            if (pos.x < *x0) *x0 = pos.x;
            if (pos.y < *y0) *y0 = pos.y;
            if (pos.x + pos.width > *x1) *x1 = pos.x + pos.width;
            if (pos.y + pos.height > *y1) *y1 = pos.y + pos.height;
        }
    }

    const int n = (int) el->get_children_count();
    int i = 0;

    while (i < n)
    {
        unionBox (el->get_child (i), found, x0, y0, x1, y1);
        i = i + 1;
    }
}

static void walk (litehtml::element::ptr el, std::ownvector<Link>* out)
{
    if (! el)
        return;

    const char* tag = el->get_tagName();

    if (tag != 0 && strcmp (tag, "a") == 0)
    {
        // el_anchor derives from html_tag, so get_attr is the real one rather
        // than element's stub. (That stub is the same bug el_script hits.)
        const litehtml::tchar_t* href = el->get_attr ("href");

        if (href != 0 && href[0] != '\0')
        {
            int found = 0;
            int x0 = 0;
            int y0 = 0;
            int x1 = 0;
            int y1 = 0;

            unionBox (el, &found, &x0, &y0, &x1, &y1);

            Link link;
            link.href.assign (href);

            if (found != 0)
            {
                link.x = x0;
                link.y = y0;
                link.width = x1 - x0;
                link.height = y1 - y0;
            }
            else
            {
                litehtml::position pos = el->get_placement();
                link.x = pos.x;
                link.y = pos.y;
                link.width = pos.width;
                link.height = pos.height;
            }

            litehtml::tstring text;
            el->get_text (text);
            link.text.assign (text.c_str());

            out->push_back (link);
        }
    }

    const int n = (int) el->get_children_count();
    int i = 0;

    while (i < n)
    {
        walk (el->get_child (i), out);
        i = i + 1;
    }
}

void collectLinks (litehtml::document::ptr doc, std::ownvector<Link>* out)
{
    if (out == 0)
        return;

    out->clear();

    if (doc)
        walk (doc->root(), out);
}

int resolveHref (const char* baseDir, const char* href, std::string* out)
{
    if (href == 0 || out == 0 || href[0] == '\0')
        return 0;

    // A fragment stays on the page, and a remote URL is not this front end's
    // to fetch -- network loading belongs to a loader that does not exist here.
    if (href[0] == '#')
        return 0;

    // http and https are refused here because this resolves to a local path.
    // A remote document goes through resolveUrl instead, where they are the
    // normal case rather than the exception.
    if (strncmp (href, "http://", 7) == 0
        || strncmp (href, "https://", 8) == 0
        || strncmp (href, "mailto:", 7) == 0)
        return 0;

    const char* rel = href;

    if (strncmp (href, "file://", 7) == 0)
        rel = href + 7;

    // Strip any fragment or query from a local path.
    std::string clean;
    int i = 0;

    while (rel[i] != '\0' && rel[i] != '#' && rel[i] != '?')
    {
        clean.push_back (rel[i]);
        i = i + 1;
    }

    if (clean.empty())
        return 0;

    out->clear();

    if (clean[0] != '/' && baseDir != 0 && baseDir[0] != '\0')
    {
        out->assign (baseDir);
        out->append ("/");
        out->append (clean.c_str());
    }
    else
    {
        out->assign (clean.c_str());
    }

    return 1;
}

} // namespace headless
