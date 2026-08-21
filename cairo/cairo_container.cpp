#include "cairo_container.h"

#include <string.h>

namespace headless {

//==============================================================================

CairoFont::CairoFont()
{
    measureContext = 0;
}

CairoFont::~CairoFont()
{
    // The measuring context belongs to the container.
}

void CairoFont::selectOn (cairo_t* cr)
{
    if (cr == 0)
        return;

    cairo_font_slant_t slant = CAIRO_FONT_SLANT_NORMAL;

    if (italic != 0)
        slant = CAIRO_FONT_SLANT_ITALIC;

    cairo_font_weight_t w = CAIRO_FONT_WEIGHT_NORMAL;

    if (weight >= 600)
        w = CAIRO_FONT_WEIGHT_BOLD;

    cairo_select_font_face (cr, face.c_str(), slant, w);
    cairo_set_font_size (cr, (double) size);
}

void CairoFont::getMetrics (litehtml::font_metrics* fm)
{
    if (fm == 0)
        return;

    if (measureContext == 0)
    {
        // No context to measure with; fall back to the synthetic metrics
        // rather than reporting zeros, which would collapse every line box.
        Font::getMetrics (fm);
        return;
    }

    selectOn (measureContext);

    cairo_font_extents_t fe;
    cairo_font_extents (measureContext, &fe);

    fm->ascent = (int) (fe.ascent + 0.5);
    fm->descent = (int) (fe.descent + 0.5);
    fm->height = (int) (fe.height + 0.5);

    // Cairo's toy API exposes no x-height, so approximate it by measuring a
    // lowercase glyph, which is what x-height means.
    cairo_text_extents_t te;
    cairo_text_extents (measureContext, "x", &te);
    fm->x_height = (int) (te.height + 0.5);

    fm->draw_spaces = (decoration != 0);
}

int CairoFont::textWidth (const char* text)
{
    if (text == 0)
        return 0;

    if (measureContext == 0)
        return Font::textWidth (text);

    selectOn (measureContext);

    cairo_text_extents_t te;
    cairo_text_extents (measureContext, text, &te);

    // x_advance, not width: trailing side bearing counts for layout.
    return (int) (te.x_advance + 0.5);
}

//==============================================================================

CairoContainer::CairoContainer()
{
    target = 0;

    // A 1x1 surface is enough to measure against; nothing is ever drawn here.
    scratch = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
    measure = cairo_create (scratch);
}

CairoContainer::~CairoContainer()
{
    // The base destructor deletes the fonts, and they refer to `measure`, so
    // it has to outlive them. Members are destroyed before the base, so run
    // the font cleanup here before releasing the context.
    if (measure != 0)
        cairo_destroy (measure);

    if (scratch != 0)
        cairo_surface_destroy (scratch);

    measure = 0;
    scratch = 0;
}

void CairoContainer::setTarget (cairo_t* cr)
{
    target = cr;
}

litehtml::uint_ptr CairoContainer::create_font (const litehtml::tchar_t* faceName,
                                                int size,
                                                int weight,
                                                litehtml::font_style italic,
                                                unsigned int decoration,
                                                litehtml::font_metrics* fm)
{
    // Let the base class do the font-stack parsing and bookkeeping, then
    // upgrade the result in place. Reimplementing the stack parsing here is
    // exactly the duplication subclassing is meant to avoid, but the base
    // allocates a plain Font, so instead build a CairoFont and copy the
    // parsed fields across.
    litehtml::uint_ptr base = Container::create_font (faceName, size, weight,
                                                      italic, decoration, fm);
    Font* parsed = (Font*) base;

    if (parsed == 0)
        return 0;

    CairoFont* font = new CairoFont();
    font->face.assign (parsed->face.c_str());
    font->size = parsed->size;
    font->weight = parsed->weight;
    font->italic = parsed->italic;
    font->decoration = parsed->decoration;
    font->monospace = parsed->monospace;
    font->measureContext = measure;

    // Drop the base's plain Font and register the real one in its place.
    Container::delete_font (base);

    fonts.push_back (font);
    font->getMetrics (fm);

    return (litehtml::uint_ptr) font;
}

int CairoContainer::text_width (const litehtml::tchar_t* text, litehtml::uint_ptr hFont)
{
    CairoFont* font = (CairoFont*) hFont;

    if (font == 0)
        return 0;

    return font->textWidth (text);
}

//==============================================================================

static void setSourceColor (cairo_t* cr, litehtml::web_color color)
{
    cairo_set_source_rgba (cr,
                           ((double) color.red) / 255.0,
                           ((double) color.green) / 255.0,
                           ((double) color.blue) / 255.0,
                           ((double) color.alpha) / 255.0);
}

void CairoContainer::draw_text (litehtml::uint_ptr hdc,
                                const litehtml::tchar_t* text,
                                litehtml::uint_ptr hFont,
                                litehtml::web_color color,
                                const litehtml::position& pos)
{
    // Keep recording, so the dump modes and the golden tests still work on a
    // container that also draws.
    Container::draw_text (hdc, text, hFont, color, pos);

    CairoFont* font = (CairoFont*) hFont;

    if (target == 0 || font == 0 || text == 0)
        return;

    font->selectOn (target);
    setSourceColor (target, color);

    cairo_font_extents_t fe;
    cairo_font_extents (target, &fe);

    // litehtml gives the top of the line box; cairo draws from the baseline.
    cairo_move_to (target, (double) pos.x, ((double) pos.y) + fe.ascent);
    cairo_show_text (target, text);
}

void CairoContainer::draw_background (litehtml::uint_ptr hdc, const litehtml::background_paint* bg)
{
    Container::draw_background (hdc, bg);

    if (target == 0 || bg->color.alpha == 0)
        return;

    setSourceColor (target, bg->color);
    cairo_rectangle (target,
                     (double) bg->border_box.x, (double) bg->border_box.y,
                     (double) bg->border_box.width, (double) bg->border_box.height);
    cairo_fill (target);
}

void CairoContainer::draw_borders (litehtml::uint_ptr hdc,
                                   const litehtml::borders& borders,
                                   const litehtml::position& draw_pos,
                                   bool root)
{
    Container::draw_borders (hdc, borders, draw_pos, root);

    if (target == 0)
        return;

    const double x = (double) draw_pos.x;
    const double y = (double) draw_pos.y;
    const double w = (double) draw_pos.width;
    const double h = (double) draw_pos.height;

    if (borders.top.width > 0)
    {
        setSourceColor (target, borders.top.color);
        cairo_set_line_width (target, (double) borders.top.width);
        cairo_move_to (target, x, y);
        cairo_line_to (target, x + w, y);
        cairo_stroke (target);
    }

    if (borders.bottom.width > 0)
    {
        setSourceColor (target, borders.bottom.color);
        cairo_set_line_width (target, (double) borders.bottom.width);
        cairo_move_to (target, x, y + h);
        cairo_line_to (target, x + w, y + h);
        cairo_stroke (target);
    }

    if (borders.left.width > 0)
    {
        setSourceColor (target, borders.left.color);
        cairo_set_line_width (target, (double) borders.left.width);
        cairo_move_to (target, x, y);
        cairo_line_to (target, x, y + h);
        cairo_stroke (target);
    }

    if (borders.right.width > 0)
    {
        setSourceColor (target, borders.right.color);
        cairo_set_line_width (target, (double) borders.right.width);
        cairo_move_to (target, x + w, y);
        cairo_line_to (target, x + w, y + h);
        cairo_stroke (target);
    }
}

void CairoContainer::draw_list_marker (litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
    Container::draw_list_marker (hdc, marker);

    if (target == 0)
        return;

    setSourceColor (target, marker.color);

    const double r = ((double) marker.pos.width) / 2.0;

    cairo_arc (target,
               ((double) marker.pos.x) + r,
               ((double) marker.pos.y) + r,
               r, 0.0, 6.28318530718);
    cairo_fill (target);
}

} // namespace headless
