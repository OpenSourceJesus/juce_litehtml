#include "dump.h"
#include "strbuf.h"

#include <string.h>

namespace headless {

//==============================================================================

static const char* displayName (litehtml::style_display d)
{
    if (d == litehtml::display_none)               return "none";
    if (d == litehtml::display_block)              return "block";
    if (d == litehtml::display_inline)             return "inline";
    if (d == litehtml::display_inline_block)       return "inline-block";
    if (d == litehtml::display_inline_table)       return "inline-table";
    if (d == litehtml::display_list_item)          return "list-item";
    if (d == litehtml::display_table)              return "table";
    if (d == litehtml::display_table_caption)      return "table-caption";
    if (d == litehtml::display_table_cell)         return "table-cell";
    if (d == litehtml::display_table_column)       return "table-column";
    if (d == litehtml::display_table_column_group) return "table-column-group";
    if (d == litehtml::display_table_footer_group) return "table-footer-group";
    if (d == litehtml::display_table_header_group) return "table-header-group";
    if (d == litehtml::display_table_row)          return "table-row";
    if (d == litehtml::display_table_row_group)    return "table-row-group";
    if (d == litehtml::display_inline_text)        return "inline-text";
    return "?";
}

static const char* commandName (int type)
{
    if (type == DrawTypeText)       return "text";
    if (type == DrawTypeBackground) return "background";
    if (type == DrawTypeBorders)    return "borders";
    if (type == DrawTypeListMarker) return "marker";
    return "?";
}

/** Appends "(x,y wxh)". */
static void appendBox (std::string* out, int x, int y, int w, int h)
{
    strAppend (out, "(");
    strAppendInt (out, x);
    strAppend (out, ",");
    strAppendInt (out, y);
    strAppend (out, " ");
    strAppendInt (out, w);
    strAppend (out, "x");
    strAppendInt (out, h);
    strAppend (out, ")");
}

static void dumpElement (litehtml::element::ptr el, int depth, std::string* out)
{
    if (! el)
        return;

    litehtml::position pos = el->get_placement();
    const char* tag = el->get_tagName();

    strAppendSpaces (out, depth * 2);
    strAppend (out, (tag != 0) ? tag : "?");
    strAppend (out, " [");
    strAppend (out, displayName (el->get_display()));
    strAppend (out, "] ");
    appendBox (out, pos.x, pos.y, pos.width, pos.height);

    if (el->get_children_count() == 0)
    {
        litehtml::tstring text;
        el->get_text (text);

        std::string summary;
        strAppendSummary (&summary, text.c_str(), 48);

        if (! summary.empty())
        {
            strAppend (out, " \"");
            out->append (summary.c_str());
            strAppend (out, "\"");
        }
    }

    strAppend (out, "\n");

    const int n = (int) el->get_children_count();
    int i = 0;

    while (i < n)
    {
        dumpElement (el->get_child (i), depth + 1, out);
        i = i + 1;
    }
}

//==============================================================================

void dumpLayoutTree (litehtml::document::ptr doc, std::string* out)
{
    if (doc)
        dumpElement (doc->root(), 0, out);
}

void dumpDisplayList (Container* container, std::string* out)
{
    std::ownvector<DrawCommand>* commands = container->getDrawCommands();
    const int n = (int) commands->size();
    int i = 0;

    while (i < n)
    {
        DrawCommand& cmd = (*commands)[i];

        strAppend (out, "[");
        strAppendInt (out, i);
        strAppend (out, "] ");
        strAppend (out, commandName (cmd.type));
        strAppend (out, " ");
        appendBox (out, cmd.x, cmd.y, cmd.width, cmd.height);
        strAppend (out, " ");
        out->append (cmd.color.c_str());

        if (cmd.font != 0)
        {
            strAppend (out, " ");
            out->append (cmd.font->face.c_str());
            strAppend (out, "/");
            strAppendInt (out, cmd.font->size);

            if (cmd.font->weight >= 600)
                strAppend (out, "/bold");

            if (cmd.font->italic != 0)
                strAppend (out, "/italic");
        }

        if (! cmd.text.empty())
        {
            strAppend (out, " \"");
            strAppendSummary (out, cmd.text.c_str(), 64);
            strAppend (out, "\"");
        }

        strAppend (out, "\n");
        i = i + 1;
    }
}

//==============================================================================

/** One piece of text at a position: what the text reconstruction works from. */
class TextRun
{
public:
    int x;
    int y;
    int width;
    int height;
    std::string text;

    TextRun();
};

TextRun::TextRun()
{
    x = 0;
    y = 0;
    width = 0;
    height = 0;
}

/** Insertion sort by y then x.

    Written out rather than calling std::stable_sort, which is outside the
    subset. Insertion sort is stable, and the display lists here are small
    enough that the quadratic cost does not matter.
 */
static void sortRunsByPosition (std::ownvector<TextRun>* runs)
{
    const int n = (int) runs->size();
    int i = 1;

    while (i < n)
    {
        int j = i;

        while (j > 0)
        {
            TextRun& a = (*runs)[j - 1];
            TextRun& b = (*runs)[j];

            int swap = 0;

            if (b.y < a.y)
                swap = 1;
            else if (b.y == a.y && b.x < a.x)
                swap = 1;

            if (swap == 0)
                break;

            TextRun tmp = a;
            a = b;
            b = tmp;

            j = j - 1;
        }

        i = i + 1;
    }
}

/** Sorts one span of an already-grouped run list left to right. */
static void sortSpanByX (std::ownvector<TextRun>* runs, int from, int to)
{
    int i = from + 1;

    while (i < to)
    {
        int j = i;

        while (j > from)
        {
            TextRun& a = (*runs)[j - 1];
            TextRun& b = (*runs)[j];

            if (b.x >= a.x)
                break;

            TextRun tmp = a;
            a = b;
            b = tmp;

            j = j - 1;
        }

        i = i + 1;
    }
}

void dumpText (Container* container, std::string* out)
{
    std::ownvector<DrawCommand>* commands = container->getDrawCommands();
    std::ownvector<TextRun> runs;

    int i = 0;

    while (i < (int) commands->size())
    {
        DrawCommand& cmd = (*commands)[i];

        if (cmd.type == DrawTypeText && ! cmd.text.empty())
        {
            TextRun run;
            run.x = cmd.x;
            run.y = cmd.y;
            run.width = cmd.width;
            run.height = cmd.height;
            run.text.assign (cmd.text.c_str());
            runs.push_back (run);
        }
        else if (cmd.type == DrawTypeListMarker)
        {
            TextRun run;
            run.x = cmd.x;
            run.y = cmd.y;
            run.width = cmd.width;
            run.height = cmd.height;
            run.text.assign ("\u2022");
            runs.push_back (run);
        }

        i = i + 1;
    }

    sortRunsByPosition (&runs);

    const int n = (int) runs.size();
    int lineStart = 0;

    // Runs belong to one line when they overlap it vertically. That keeps
    // list markers, superscripts and mixed font sizes on the line they were
    // painted on, where a plain baseline comparison would split them apart.
    while (lineStart < n)
    {
        int lineTop = runs[lineStart].y;
        int lineBottom = runs[lineStart].y + runs[lineStart].height;

        if (runs[lineStart].height < 1)
            lineBottom = runs[lineStart].y + 1;

        int lineEnd = lineStart + 1;

        while (lineEnd < n)
        {
            TextRun& r = runs[lineEnd];
            int rBottom = r.y + r.height;

            if (r.height < 1)
                rBottom = r.y + 1;

            if (! (r.y < lineBottom && rBottom > lineTop))
                break;

            if (r.y < lineTop)
                lineTop = r.y;

            if (rBottom > lineBottom)
                lineBottom = rBottom;

            lineEnd = lineEnd + 1;
        }

        // Paint order is not left-to-right order.
        sortSpanByX (&runs, lineStart, lineEnd);

        int cursorX = runs[lineStart].x;
        int k = lineStart;

        while (k < lineEnd)
        {
            TextRun& r = runs[k];

            // litehtml paints each word separately and usually omits the
            // spaces between them, so reinstate a space wherever two runs
            // are not touching.
            if (r.x > cursorX + 1)
                out->push_back (' ');

            out->append (r.text.c_str());
            cursorX = r.x + r.width;
            k = k + 1;
        }

        out->push_back ('\n');
        lineStart = lineEnd;
    }
}

//==============================================================================

void dumpAscii (Container* container, int pageWidth, int pageHeight, std::string* out)
{
    // One cell per 6x12 pixel block.
    const int cellW = 6;
    const int cellH = 12;

    int cols = (pageWidth + cellW - 1) / cellW;
    int rows = (pageHeight + cellH - 1) / cellH;

    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (rows > 400) rows = 400;

    // Flat char grid, plus a parallel mask of which cells hold text. Text may
    // overwrite box drawing -- a cell border must not push its own contents
    // out of the cell -- but must not overwrite other text.
    const int total = rows * cols;
    std::string grid;
    std::string mask;

    int fill = 0;

    while (fill < total)
    {
        grid.push_back (' ');
        mask.push_back ('0');
        fill = fill + 1;
    }

    std::ownvector<DrawCommand>* commands = container->getDrawCommands();
    int i = 0;

    while (i < (int) commands->size())
    {
        DrawCommand& cmd = (*commands)[i];

        if (cmd.type == DrawTypeBorders)
        {
            int c0 = cmd.x / cellW;
            int r0 = cmd.y / cellH;
            int c1 = (cmd.x + cmd.width - 1) / cellW;
            int r1 = (cmd.y + cmd.height - 1) / cellH;

            int c = c0;

            while (c <= c1)
            {
                if (c >= 0 && c < cols)
                {
                    if (r0 >= 0 && r0 < rows) grid[r0 * cols + c] = '-';
                    if (r1 >= 0 && r1 < rows) grid[r1 * cols + c] = '-';
                }

                c = c + 1;
            }

            int r = r0;

            while (r <= r1)
            {
                if (r >= 0 && r < rows)
                {
                    if (c0 >= 0 && c0 < cols) grid[r * cols + c0] = '|';
                    if (c1 >= 0 && c1 < cols) grid[r * cols + c1] = '|';
                }

                r = r + 1;
            }
        }
        else if (cmd.type == DrawTypeListMarker)
        {
            int c = cmd.x / cellW;
            int r = cmd.y / cellH;

            if (c >= 0 && c < cols && r >= 0 && r < rows)
                grid[r * cols + c] = '*';
        }
        else if (cmd.type == DrawTypeText)
        {
            int row = cmd.y / cellH;
            int col = cmd.x / cellW;

            if (row >= 0 && row < rows)
            {
                // Nudge right past text already written, then leave one blank
                // cell as a word separator. A capturing lambda cannot be used
                // in a loop condition in this subset, so the test is inline.
                while (col < cols && col >= 0 && mask[row * cols + col] == '1')
                    col = col + 1;

                if (col > 0 && col <= cols && mask[row * cols + col - 1] == '1')
                    col = col + 1;

                int p = 0;

                while (cmd.text[p] != '\0')
                {
                    const char ch = cmd.text[p];
                    p = p + 1;

                    // Skip UTF-8 continuation bytes so a multi-byte character
                    // occupies a single cell.
                    if (((unsigned char) ch & 0xc0) == 0x80)
                        continue;

                    if (col >= 0 && col < cols)
                    {
                        char plotted = ch;

                        if (ch == '\t' || ch == '\n')
                            plotted = ' ';

                        grid[row * cols + col] = plotted;
                        mask[row * cols + col] = '1';
                    }

                    col = col + 1;
                }
            }
        }

        i = i + 1;
    }

    // Emit, trimming trailing blanks on each row.
    int r2 = 0;

    while (r2 < rows)
    {
        int end = cols;

        while (end > 0 && grid[r2 * cols + end - 1] == ' ')
            end = end - 1;

        int c2 = 0;

        while (c2 < end)
        {
            out->push_back (grid[r2 * cols + c2]);
            c2 = c2 + 1;
        }

        out->push_back ('\n');
        r2 = r2 + 1;
    }
}

} // namespace headless
