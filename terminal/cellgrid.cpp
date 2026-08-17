#include "cellgrid.h"
#include "../headless/strbuf.h"

namespace headless {

//==============================================================================

unsigned gridDecodeUtf8 (const char* text, int* pos)
{
    int i = *pos;
    const unsigned char c = (unsigned char) text[i];

    if (c < 0x80)
    {
        *pos = i + 1;
        return (unsigned) c;
    }

    int extra = 0;
    unsigned cp = 0;

    if ((c & 0xe0) == 0xc0)      { extra = 1; cp = (unsigned) (c & 0x1f); }
    else if ((c & 0xf0) == 0xe0) { extra = 2; cp = (unsigned) (c & 0x0f); }
    else if ((c & 0xf8) == 0xf0) { extra = 3; cp = (unsigned) (c & 0x07); }
    else                         { *pos = i + 1; return (unsigned) c; }

    i = i + 1;
    int taken = 0;

    while (taken < extra && ((unsigned char) text[i] & 0xc0) == 0x80)
    {
        cp = (cp << 6) | (unsigned) ((unsigned char) text[i] & 0x3f);
        i = i + 1;
        taken = taken + 1;
    }

    *pos = i;
    return cp;
}

void gridAppendUtf8 (std::string* out, unsigned cp)
{
    if (out == 0)
        return;

    if (cp < 0x80)
    {
        out->push_back ((char) cp);
    }
    else if (cp < 0x800)
    {
        out->push_back ((char) (0xc0 | (cp >> 6)));
        out->push_back ((char) (0x80 | (cp & 0x3f)));
    }
    else if (cp < 0x10000)
    {
        out->push_back ((char) (0xe0 | (cp >> 12)));
        out->push_back ((char) (0x80 | ((cp >> 6) & 0x3f)));
        out->push_back ((char) (0x80 | (cp & 0x3f)));
    }
    else
    {
        out->push_back ((char) (0xf0 | (cp >> 18)));
        out->push_back ((char) (0x80 | ((cp >> 12) & 0x3f)));
        out->push_back ((char) (0x80 | ((cp >> 6) & 0x3f)));
        out->push_back ((char) (0x80 | (cp & 0x3f)));
    }
}

int codepointColumns (unsigned cp)
{
    // The East Asian Wide and Fullwidth ranges, matching the width table the
    // synthetic font uses so layout and display agree.
    if (cp >= 0x1100 && cp <= 0x115f) return 2;
    if (cp >= 0x2e80 && cp <= 0xa4cf) return 2;
    if (cp >= 0xac00 && cp <= 0xd7a3) return 2;
    if (cp >= 0xf900 && cp <= 0xfaff) return 2;
    if (cp >= 0xff00 && cp <= 0xff60) return 2;
    if (cp >= 0x20000 && cp <= 0x3fffd) return 2;
    return 1;
}

int textColumns (const char* utf8)
{
    if (utf8 == 0)
        return 0;

    int total = 0;
    int pos = 0;

    while (utf8[pos] != '\0')
    {
        const unsigned cp = gridDecodeUtf8 (utf8, &pos);
        total = total + codepointColumns (cp);
    }

    return total;
}

//==============================================================================

Cell::Cell()
{
    ch = 32;                 // space
    fg = kColorDefault;
    bg = kColorDefault;
    attrs = 0;
}

CellGrid::CellGrid()
{
    cols = 0;
    rows = 0;
}

void CellGrid::resize (int newCols, int newRows)
{
    if (newCols < 1) newCols = 1;
    if (newRows < 1) newRows = 1;

    cols = newCols;
    rows = newRows;

    cells.clear();

    const int total = cols * rows;
    int i = 0;

    while (i < total)
    {
        Cell c;
        cells.push_back (c);
        i = i + 1;
    }
}

void CellGrid::clear()
{
    const int total = cols * rows;
    int i = 0;

    while (i < total)
    {
        Cell c;
        cells[i] = c;
        i = i + 1;
    }
}

int CellGrid::getCols() { return cols; }
int CellGrid::getRows() { return rows; }

Cell* CellGrid::at (int col, int row)
{
    if (col < 0 || col >= cols || row < 0 || row >= rows)
        return 0;

    return &cells[row * cols + col];
}

void CellGrid::fillRect (int col, int row, int w, int h, int bg)
{
    int r = row;

    while (r < row + h)
    {
        int c = col;

        while (c < col + w)
        {
            Cell* cell = at (c, r);

            if (cell != 0)
                cell->bg = bg;

            c = c + 1;
        }

        r = r + 1;
    }
}

void CellGrid::putChar (int col, int row, unsigned cp, int fg, int attrs)
{
    Cell* cell = at (col, row);

    if (cell == 0)
        return;

    cell->ch = cp;
    cell->fg = fg;
    cell->attrs = attrs;

    if (codepointColumns (cp) == 2)
    {
        // Mark the trailing half so a backend does not print a second glyph.
        Cell* tail = at (col + 1, row);

        if (tail != 0)
        {
            tail->ch = 0;
            tail->fg = fg;
            tail->attrs = attrs;
        }
    }
}

void CellGrid::putText (int col, int row, const char* utf8, int fg, int attrs)
{
    if (utf8 == 0)
        return;

    int pos = 0;
    int c = col;

    while (utf8[pos] != '\0')
    {
        const unsigned cp = gridDecodeUtf8 (utf8, &pos);

        if (cp == 9 || cp == 10 || cp == 13)
        {
            putChar (c, row, 32, fg, attrs);
            c = c + 1;
            continue;
        }

        putChar (c, row, cp, fg, attrs);
        c = c + codepointColumns (cp);
    }
}

int CellGrid::spanHasContent (int col, int row, int w)
{
    int c = col;

    while (c < col + w)
    {
        Cell* cell = at (c, row);

        if (cell != 0 && cell->ch != 32 && cell->ch != 0)
            return 1;

        c = c + 1;
    }

    return 0;
}

void CellGrid::drawBox (int col, int row, int w, int h, int fg)
{
    if (w < 1 || h < 1)
        return;

    // A box needs a rule, a row of content and another rule. Anything
    // shorter cannot be drawn without either cutting through its own text or
    // leaving orphaned corners, so it is left undrawn. Dense table borders
    // are simply not expressible at one row per line.
    if (h < 3)
        return;

    const int c1 = col + w - 1;
    const int r1 = row + h - 1;

    // A box is only 1.5 rows tall for a typical table cell, so a rule and the
    // cell's own text land on the same row. Text wins: the rule is dropped
    // for that row entirely rather than drawn through the gaps between words,
    // which is what made tables unreadable.
    const int topClear = (spanHasContent (col, row, w) == 0) ? 1 : 0;
    const int bottomClear = (spanHasContent (col, r1, w) == 0) ? 1 : 0;

    int c = col + 1;

    while (c < c1)
    {
        Cell* top = at (c, row);
        Cell* bottom = at (c, r1);

        if (topClear != 0 && top != 0 && top->ch == 32) { top->ch = 0x2500; top->fg = fg; }
        if (bottomClear != 0 && bottom != 0 && bottom->ch == 32) { bottom->ch = 0x2500; bottom->fg = fg; }

        c = c + 1;
    }

    int r = row + 1;

    while (r < r1)
    {
        Cell* left = at (col, r);
        Cell* right = at (c1, r);

        if (left != 0 && left->ch == 32) { left->ch = 0x2502; left->fg = fg; }
        if (right != 0 && right->ch == 32) { right->ch = 0x2502; right->fg = fg; }

        r = r + 1;
    }

    Cell* tl = at (col, row);
    Cell* tr = at (c1, row);
    Cell* bl = at (col, r1);
    Cell* br = at (c1, r1);

    if (topClear != 0 && tl != 0 && tl->ch == 32) { tl->ch = 0x250c; tl->fg = fg; }
    if (topClear != 0 && tr != 0 && tr->ch == 32) { tr->ch = 0x2510; tr->fg = fg; }
    if (bottomClear != 0 && bl != 0 && bl->ch == 32) { bl->ch = 0x2514; bl->fg = fg; }
    if (bottomClear != 0 && br != 0 && br->ch == 32) { br->ch = 0x2518; br->fg = fg; }
}

int CellGrid::lastUsedRow()
{
    int last = -1;
    int r = 0;

    while (r < rows)
    {
        int c = 0;

        while (c < cols)
        {
            Cell* cell = at (c, r);

            if (cell != 0)
            {
                if (cell->ch != 32 || cell->bg != kColorDefault)
                {
                    last = r;
                    c = cols;
                }
            }

            c = c + 1;
        }

        r = r + 1;
    }

    return last;
}

//==============================================================================

static void appendSgrColor (std::string* out, int rgba, int isBackground)
{
    // Truecolor. Terminals that do not support it ignore the sequence rather
    // than printing it, and --no-color exists for the ones that do not.
    const int r = (rgba >> 24) & 255;
    const int g = (rgba >> 16) & 255;
    const int b = (rgba >> 8) & 255;

    strAppend (out, "\x1b[");
    strAppend (out, (isBackground != 0) ? "48" : "38");
    strAppend (out, ";2;");
    strAppendInt (out, r);
    strAppend (out, ";");
    strAppendInt (out, g);
    strAppend (out, ";");
    strAppendInt (out, b);
    strAppend (out, "m");
}

void CellGrid::toAnsi (std::string* out, int color, int maxRows)
{
    int limit = rows;

    if (maxRows >= 0 && maxRows < rows)
        limit = maxRows;

    int r = 0;

    while (r < limit)
    {
        int lineEnd = cols;

        // Trim trailing cells that would print as nothing.
        while (lineEnd > 0)
        {
            Cell* cell = at (lineEnd - 1, r);

            if (cell == 0)
                break;

            if (cell->ch != 32 || cell->bg != kColorDefault)
                break;

            lineEnd = lineEnd - 1;
        }

        int curFg = kColorDefault;
        int curBg = kColorDefault;
        int curAttrs = 0;
        int c = 0;

        while (c < lineEnd)
        {
            Cell* cell = at (c, r);

            if (cell == 0)
                break;

            if (cell->ch == 0)
            {
                // Trailing half of a wide glyph; already emitted.
                c = c + 1;
                continue;
            }

            if (color != 0)
            {
                if (cell->attrs != curAttrs)
                {
                    strAppend (out, "\x1b[0m");
                    curFg = kColorDefault;
                    curBg = kColorDefault;

                    if ((cell->attrs & CellAttrBold) != 0)
                        strAppend (out, "\x1b[1m");

                    if ((cell->attrs & CellAttrItalic) != 0)
                        strAppend (out, "\x1b[3m");

                    if ((cell->attrs & CellAttrUnderline) != 0)
                        strAppend (out, "\x1b[4m");

                    curAttrs = cell->attrs;
                }

                if (cell->fg != curFg)
                {
                    if (cell->fg == kColorDefault)
                        strAppend (out, "\x1b[39m");
                    else
                        appendSgrColor (out, cell->fg, 0);

                    curFg = cell->fg;
                }

                if (cell->bg != curBg)
                {
                    if (cell->bg == kColorDefault)
                        strAppend (out, "\x1b[49m");
                    else
                        appendSgrColor (out, cell->bg, 1);

                    curBg = cell->bg;
                }
            }

            gridAppendUtf8 (out, cell->ch);
            c = c + 1;
        }

        if (color != 0 && (curFg != kColorDefault || curBg != kColorDefault || curAttrs != 0))
            strAppend (out, "\x1b[0m");

        out->push_back ('\n');
        r = r + 1;
    }
}

} // namespace headless
