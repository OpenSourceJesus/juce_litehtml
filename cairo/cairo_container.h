#pragma once

#include <cairo/cairo.h>

#include "../headless/container.h"

namespace headless {

/** A font backed by a real cairo font face.

    Subclasses the synthetic Font rather than replacing it, so everything that
    already reads Font -- the display list, the dump functions, the tests --
    keeps working unchanged. Only the two metric methods are overridden.
 */
class CairoFont : public Font
{
public:
    CairoFont();
    virtual ~CairoFont();

    /** Selects this font on a cairo context, ready to measure or draw. */
    void selectOn (cairo_t* cr);

    /** The measuring context is owned by the container, not by the font. */
    cairo_t* measureContext;

    virtual void getMetrics (litehtml::font_metrics* fm);
    virtual int textWidth (const char* text);
};

/** A container that draws with cairo.

    Subclasses the headless container, which is the whole point of the split:
    image sizing, css and script loading, media features, transform_text,
    create_element and the display list are all inherited unchanged. What is
    overridden is exactly the part that needs a graphics library -- fonts and
    the four paint calls.

    Rendering goes to a cairo surface, so this needs no window. GTK supplies
    one on top; see gtk/.
 */
class CairoContainer : public Container
{
public:
    CairoContainer();
    virtual ~CairoContainer();

    /** Points the container at the surface to draw on. The container does not
        take ownership.
     */
    void setTarget (cairo_t* cr);

    virtual litehtml::uint_ptr create_font (const litehtml::tchar_t* faceName,
                                            int size,
                                            int weight,
                                            litehtml::font_style italic,
                                            unsigned int decoration,
                                            litehtml::font_metrics* fm);
    virtual int text_width (const litehtml::tchar_t* text, litehtml::uint_ptr hFont);
    virtual void draw_text (litehtml::uint_ptr hdc,
                            const litehtml::tchar_t* text,
                            litehtml::uint_ptr hFont,
                            litehtml::web_color color,
                            const litehtml::position& pos);
    virtual void draw_background (litehtml::uint_ptr hdc, const litehtml::background_paint* bg);
    virtual void draw_borders (litehtml::uint_ptr hdc,
                               const litehtml::borders& borders,
                               const litehtml::position& draw_pos,
                               bool root);
    virtual void draw_list_marker (litehtml::uint_ptr hdc, const litehtml::list_marker& marker);

protected:
    cairo_t* target;            // borrowed
    cairo_surface_t* scratch;   // owned, used only for measuring
    cairo_t* measure;           // owned, used only for measuring
};

} // namespace headless
