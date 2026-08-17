#include "gtk_container.h"

#include <string.h>

namespace headless {

PixbufEntry::PixbufEntry()
{
    pixbuf = 0;
}

//==============================================================================

GtkContainer::GtkContainer()
{
    cursorName.assign ("auto");
}

GtkContainer::~GtkContainer()
{
    clearImages();
}

void GtkContainer::clearImages()
{
    int i = 0;

    while (i < (int) pixbufs.size())
    {
        if (pixbufs[i].pixbuf != 0)
            g_object_unref (pixbufs[i].pixbuf);

        pixbufs[i].pixbuf = 0;
        i = i + 1;
    }

    pixbufs.clear();
}

const char* GtkContainer::getCursorName()
{
    return cursorName.c_str();
}

void GtkContainer::set_cursor (const litehtml::tchar_t* cursor)
{
    CairoContainer::set_cursor (cursor);

    if (cursor != 0)
        cursorName.assign (cursor);
    else
        cursorName.assign ("auto");
}

//==============================================================================

GdkPixbuf* GtkContainer::pixbufFor (const char* src)
{
    if (src == 0 || src[0] == '\0')
        return 0;

    int i = 0;

    while (i < (int) pixbufs.size())
    {
        if (strcmp (pixbufs[i].src.c_str(), src) == 0)
            return pixbufs[i].pixbuf;

        i = i + 1;
    }

    // Not seen before. Fetch through the base container's loader, so this
    // shares the cache with the sizing pass and a remote image costs one
    // request rather than two.
    PixbufEntry entry;
    entry.src.assign (src);
    entry.pixbuf = 0;

    std::string bytes;

    if (fetchImage (src, &bytes) != 0 && bytes.size() > 0)
    {
        GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
        GError* error = 0;

        // Writing by length, not as a C string: image data is binary and
        // stops at no NUL.
        if (gdk_pixbuf_loader_write (loader,
                                     (const guchar*) bytes.c_str(),
                                     (gsize) bytes.size(),
                                     &error) != FALSE)
        {
            if (gdk_pixbuf_loader_close (loader, &error) != FALSE)
            {
                GdkPixbuf* p = gdk_pixbuf_loader_get_pixbuf (loader);

                if (p != 0)
                    entry.pixbuf = (GdkPixbuf*) g_object_ref (p);
            }
        }
        else
        {
            gdk_pixbuf_loader_close (loader, 0);
        }

        if (error != 0)
            g_error_free (error);

        g_object_unref (loader);
    }

    // Cached even on failure, so a broken image is not re-fetched and
    // re-decoded on every redraw.
    pixbufs.push_back (entry);

    const int last = (int) pixbufs.size() - 1;
    return pixbufs[last].pixbuf;
}

void GtkContainer::draw_background (litehtml::uint_ptr hdc, const litehtml::background_paint& bg)
{
    // Records the display list and fills the background colour.
    CairoContainer::draw_background (hdc, bg);

    if (target == 0 || bg.image.empty())
        return;

    GdkPixbuf* pixbuf = pixbufFor (bg.image.c_str());

    if (pixbuf == 0)
        return;

    const int srcW = gdk_pixbuf_get_width (pixbuf);
    const int srcH = gdk_pixbuf_get_height (pixbuf);

    if (srcW < 1 || srcH < 1)
        return;

    const double destW = (double) bg.border_box.width;
    const double destH = (double) bg.border_box.height;

    if (destW < 1.0 || destH < 1.0)
        return;

    cairo_save (target);

    // Clip to the box, then scale the image onto it. litehtml has already
    // worked out the box from the intrinsic size plus any CSS, so scaling to
    // fill it is right even when the two differ.
    cairo_rectangle (target,
                     (double) bg.border_box.x, (double) bg.border_box.y,
                     destW, destH);
    cairo_clip (target);

    cairo_translate (target, (double) bg.border_box.x, (double) bg.border_box.y);
    cairo_scale (target, destW / (double) srcW, destH / (double) srcH);

    gdk_cairo_set_source_pixbuf (target, pixbuf, 0.0, 0.0);
    cairo_paint (target);

    cairo_restore (target);
}

} // namespace headless
