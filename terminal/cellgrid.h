#pragma once

#include "../headless/crust_compat.h"

namespace headless {

enum CellAttr {
    CellAttrBold = 1,
    CellAttrItalic = 2,
    CellAttrUnderline = 4
};

// A colour is packed 0xRRGGBBAA, matching DrawCommand::rgba.
// kColorDefault means "whatever the terminal already uses".
enum CellColor {
    kColorDefault = -1
};

/** One terminal cell. Plain data, so it goes in a plain vector. */
class Cell
{
public:
    unsigned ch;        // unicode code point; 0 means the tail of a wide glyph
    int fg;
    int bg;
    int attrs;

    Cell();
};

/** A grid of styled cells.

    This is the whole terminal abstraction. The container paints into it and
    a backend blits it out, so ncurses, notcurses and plain ANSI escape codes
    are three small readers of the same structure rather than three renderers.
 */
class CellGrid
{
public:
    CellGrid();

    void resize (int cols, int rows);
    void clear();

    int getCols();
    int getRows();

    /** Returns 0 when out of range, so callers test rather than clamp. */
    Cell* at (int col, int row);

    void fillRect (int col, int row, int w, int h, int bg);
    void drawBox (int col, int row, int w, int h, int fg);

    /** True when any cell in a horizontal span already holds a glyph. Used to
        keep a border rule from cutting through text that shares its row.
     */
    int spanHasContent (int col, int row, int w);

    /** Writes UTF-8 text starting at a cell. Wide code points take two cells,
        the second marked as a continuation.
     */
    void putText (int col, int row, const char* utf8, int fg, int attrs);

    void putChar (int col, int row, unsigned cp, int fg, int attrs);

    /** Serialises to ANSI escape codes. Set colour to 0 for plain text, and
        maxRows to -1 for the whole grid.
     */
    void toAnsi (std::string* out, int color, int maxRows);

    /** Row index of the last row holding anything. */
    int lastUsedRow();

private:
    std::vector<Cell> cells;
    int cols;
    int rows;
};

/** Number of terminal columns a UTF-8 string occupies. */
int textColumns (const char* utf8);

/** Columns taken by one code point: 2 for the wide ranges, else 1. */
int codepointColumns (unsigned cp);

/** Decodes one UTF-8 code point at *pos, advancing *pos. */
unsigned gridDecodeUtf8 (const char* text, int* pos);

/** Appends a code point to a string as UTF-8. */
void gridAppendUtf8 (std::string* out, unsigned cp);

} // namespace headless
