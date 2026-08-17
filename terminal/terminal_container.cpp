#include "terminal_container.h"

namespace headless {

//==============================================================================

TerminalFont::TerminalFont()
{
    cellW = 8;
    cellH = 16;
}

TerminalFont::~TerminalFont()
{
}

void TerminalFont::getMetrics (litehtml::font_metrics* fm)
{
    if (fm == 0)
        return;

    // Round the requested size up to whole rows, so a large heading occupies
    // proportionally more lines while still sitting on the grid.
    int r = (size + cellH - 1) / cellH;

    if (r < 1)
        r = 1;

    const int h = r * cellH;

    fm->height = h;
    fm->ascent = h - (h / 4);
    fm->descent = h / 4;
    fm->x_height = h / 2;
    fm->draw_spaces = (decoration != 0);
}

int TerminalFont::textWidth (const char* text)
{
    // One cell per column, whatever the nominal font size.
    return textColumns (text) * cellW;
}

//==============================================================================

TerminalContainer::TerminalContainer()
{
    cellW = 8;
    cellH = 16;
    imagePlaceholders = 1;
}

TerminalContainer::~TerminalContainer()
{
}

void TerminalContainer::setCellSize (int w, int h)
{
    if (w > 0) cellW = w;
    if (h > 0) cellH = h;
}

void TerminalContainer::setImagePlaceholders (int enabled) { imagePlaceholders = enabled; }

int TerminalContainer::getCellW() { return cellW; }
int TerminalContainer::getCellH() { return cellH; }

void TerminalContainer::setColumns (int columns)
{
    if (columns < 1)
        columns = 1;

    setViewport (columns * cellW, getViewportHeight());
}

int TerminalContainer::pixelsToCols (int px)
{
    return px / cellW;
}

int TerminalContainer::pixelsToRows (int px)
{
    return px / cellH;
}

litehtml::uint_ptr TerminalContainer::create_font (const litehtml::tchar_t* faceName,
                                                   int size,
                                                   int weight,
                                                   litehtml::font_style italic,
                                                   unsigned int decoration,
                                                   litehtml::font_metrics* fm)
{
    // Let the base parse the font stack and do the bookkeeping, then replace
    // the plain Font it registered with a cell-aligned one.
    litehtml::uint_ptr base = Container::create_font (faceName, size, weight,
                                                      italic, decoration, fm);
    Font* parsed = (Font*) base;

    if (parsed == 0)
        return 0;

    TerminalFont* font = new TerminalFont();
    font->face.assign (parsed->face.c_str());
    font->size = parsed->size;
    font->weight = parsed->weight;
    font->italic = parsed->italic;
    font->decoration = parsed->decoration;
    font->monospace = 1;            // a terminal has nothing else
    font->cellW = cellW;
    font->cellH = cellH;

    Container::delete_font (base);

    fonts.push_back (font);
    font->getMetrics (fm);

    return (litehtml::uint_ptr) font;
}

int TerminalContainer::text_width (const litehtml::tchar_t* text, litehtml::uint_ptr hFont)
{
    TerminalFont* font = (TerminalFont*) hFont;

    if (font == 0)
        return 0;

    return font->textWidth (text);
}

//==============================================================================

/** Colour is 0xRRGGBBAA; a fully transparent colour means "leave it alone". */
static int isVisible (int rgba)
{
    return ((rgba & 255) != 0) ? 1 : 0;
}

void TerminalContainer::renderToGrid (CellGrid* grid, int pageHeightPx)
{
    if (grid == 0)
        return;

    const int cols = getViewportWidth() / cellW;
    int rows = (pageHeightPx + cellH - 1) / cellH;

    if (rows < 1)
        rows = 1;

    grid->resize (cols, rows);

    std::ownvector<DrawCommand>* commands = getDrawCommands();
    const int n = (int) commands->size();

    // Three passes rather than one, because a terminal cell is far coarser
    // than a CSS pixel and the three layers collide. Backgrounds first, then
    // text, then borders last so a rule can see what text is already there
    // and get out of its way.

    int i = 0;

    while (i < n)
    {
        DrawCommand& cmd = (*commands)[i];

        if (cmd.type == DrawTypeBackground)
        {
            const int w = (cmd.width + cellW - 1) / cellW;
            const int h = (cmd.height + cellH - 1) / cellH;

            if (isVisible (cmd.rgba) != 0)
                grid->fillRect (cmd.x / cellW, cmd.y / cellH, w, h, cmd.rgba);

            // An <img> arrives as a background with an image url. A backend
            // that can draw pixels blits over this; one that cannot at least
            // shows that something is there.
            if (imagePlaceholders != 0 && ! cmd.text.empty())
            {
                const int c0 = cmd.x / cellW;
                const int r0 = cmd.y / cellH;

                grid->fillRect (c0, r0, w, h, 0x30303000 | 255);
                grid->putText (c0, r0 + (h / 2), "[img]", kColorDefault, 0);
            }
        }

        i = i + 1;
    }

    i = 0;

    while (i < n)
    {
        DrawCommand& cmd = (*commands)[i];

        if (cmd.type == DrawTypeText)
        {
            int attrs = 0;

            if (cmd.font != 0)
            {
                if (cmd.font->weight >= 600)
                    attrs = attrs | CellAttrBold;

                if (cmd.font->italic != 0)
                    attrs = attrs | CellAttrItalic;

                // litehtml's underline bit.
                if ((cmd.font->decoration & 1) != 0)
                    attrs = attrs | CellAttrUnderline;
            }

            grid->putText (cmd.x / cellW, cmd.y / cellH, cmd.text.c_str(), cmd.rgba, attrs);
        }
        else if (cmd.type == DrawTypeListMarker)
        {
            grid->putChar (cmd.x / cellW, cmd.y / cellH, 0x2022, cmd.rgba, 0);
        }

        i = i + 1;
    }

    i = 0;

    while (i < n)
    {
        DrawCommand& cmd = (*commands)[i];

        if (cmd.type == DrawTypeBorders)
        {
            const int w = (cmd.width + cellW - 1) / cellW;
            const int h = (cmd.height + cellH - 1) / cellH;
            grid->drawBox (cmd.x / cellW, cmd.y / cellH, w, h, cmd.rgba);
        }

        i = i + 1;
    }
}

} // namespace headless
