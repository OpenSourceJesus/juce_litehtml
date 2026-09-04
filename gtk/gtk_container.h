#pragma once

#include <gtk/gtk.h>

#include "../cairo/cairo_container.h"

namespace headless {

/** A decoded image, kept so a scroll or a resize does not decode it again. */
class PixbufEntry
{
public:
    std::string src;
    GdkPixbuf* pixbuf;      // owned; may be 0 when the image failed to decode

    PixbufEntry();
};

/** The GTK container.

    Subclasses CairoContainer, which already does fonts and painting -- GTK
    supplies a different cairo_t and nothing about the drawing changes. What
    this adds is images: cairo can only read PNG, while gdk-pixbuf decodes
    everything GTK ships loaders for, and the bytes come from the base
    container's loader so a remote image works the same as a local one.
 */
class GtkContainer : public CairoContainer
{
public:
    GtkContainer();
    virtual ~GtkContainer();

    /** Drops the decoded image cache, on navigating away. */
    void clearImages();

    /** Cursor litehtml last asked for, e.g. "pointer" over a link. */
    const char* getCursorName();

    virtual void draw_background (litehtml::uint_ptr hdc, const litehtml::background_paint* bg);
    virtual void set_cursor (const litehtml::tchar_t* cursor);

private:
    /** Decodes and caches, returning 0 when the image cannot be read. */
    GdkPixbuf* pixbufFor (const char* src);

    std::ownvector<PixbufEntry> pixbufs;
    std::string cursorName;
};

} // namespace headless
