#pragma once

#include "../headless/container.h"
#include "cellgrid.h"

namespace headless {

/** A font whose advances are whole terminal cells.

    This is the trick the whole terminal front end rests on. litehtml lays out
    in pixels; if one cell is exactly `cellW` pixels wide and every glyph
    advances by a whole number of cells, then every x coordinate litehtml
    produces divides exactly by cellW, and the layout lands on the cell grid
    with nothing to round. Line breaking then happens at the right column for
    free, because litehtml is measuring in the same units the terminal has.

    Font size is deliberately ignored for width: a terminal cannot draw a 28px
    heading any wider than one cell per character. It is honoured for
    *height*, rounded up to whole rows, so headings still get vertical space.
 */
class TerminalFont : public Font
{
public:
    TerminalFont();
    virtual ~TerminalFont();

    int cellW;
    int cellH;

    virtual void getMetrics (litehtml::font_metrics* fm);
    virtual int textWidth (const char* text);
};

/** A container that lays out for a character grid.

    Subclasses the headless container: image sizing, css and script loading,
    media features, transform_text and create_element are all inherited. Only
    the font metrics change, and painting is a separate pass -- the display
    list is already recorded by the base, so the grid is built from that
    rather than from overridden draw calls.
 */
class TerminalContainer : public Container
{
public:
    TerminalContainer();
    virtual ~TerminalContainer();

    /** Pixels per cell. Defaults to 8x16, which only sets the scale at which
        CSS pixel values are interpreted; nothing is drawn at that size.
     */
    void setCellSize (int w, int h);

    int getCellW();
    int getCellH();

    /** Sets the viewport from a column count. */
    void setColumns (int columns);

    /** Draw a box where an image would be. A backend that renders images for
        real (notcurses) turns this off and blits over the space instead.
     */
    void setImagePlaceholders (int enabled);

    /** Converts a pixel width to whole columns. */
    int pixelsToCols (int px);
    int pixelsToRows (int px);

    /** Paints the recorded display list into a grid. The grid is resized to
        fit the document.
     */
    void renderToGrid (CellGrid* grid, int pageHeightPx);

    virtual litehtml::uint_ptr create_font (const litehtml::tchar_t* faceName,
                                            int size,
                                            int weight,
                                            litehtml::font_style italic,
                                            unsigned int decoration,
                                            litehtml::font_metrics* fm);
    virtual int text_width (const litehtml::tchar_t* text, litehtml::uint_ptr hFont);

protected:
    int cellW;
    int cellH;
    int imagePlaceholders;
};

} // namespace headless
