// litehtml first, then curses. curses.h defines `border`, `line`, `box` and
// friends as macros, and litehtml has a class called `border` -- including
// curses first turns borders.h into a syntax error. Nothing here needs the
// litehtml headers to see curses, so ordering settles it.
#include "terminal_page.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ncursesw/curses.h>

// Interactive browser on ncurses. Same behaviour as the notcurses backend
// apart from the two things ncurses cannot do: it has no inline images, so
// the grid's [img] placeholders are left on, and it cannot take RGB, so
// colours are quantised to the 256-colour palette.

// ncurses wants colour *pairs* from a fixed table rather than direct RGB, so
// each distinct foreground gets a pair allocated on demand.
static const int kMaxPairs = 240;
static int gPairFg[241];
static int gPairCount = 0;

// Pair 0 is the terminal default and cannot be redefined, so the highlight
// takes a fixed pair above the dynamic ones.
static const int kHighlightPair = 250;

static int quantise (int rgba)
{
    const int r = (rgba >> 24) & 255;
    const int g = (rgba >> 16) & 255;
    const int b = (rgba >> 8) & 255;

    // Greys have their own ramp and look far better on it than in the cube.
    if (r == g && g == b)
    {
        if (r < 8) return 16;
        if (r > 248) return 231;
        return 232 + ((r - 8) * 23) / 240;
    }

    return 16 + 36 * ((r * 5) / 255) + 6 * ((g * 5) / 255) + ((b * 5) / 255);
}

static int pairFor (int fg)
{
    if (has_colors() == FALSE)
        return 0;

    const int colour = quantise (fg);
    int i = 0;

    while (i < gPairCount)
    {
        if (gPairFg[i] == colour)
            return i + 1;

        i = i + 1;
    }

    if (gPairCount >= kMaxPairs || gPairCount + 1 >= COLOR_PAIRS)
        return 0;

    gPairFg[gPairCount] = colour;
    gPairCount = gPairCount + 1;

    init_pair ((short) gPairCount, (short) colour, -1);
    return gPairCount;
}

//==============================================================================

class Viewer
{
public:
    Viewer();

    int run (const char* path);
    void setNetworkEnabled (int enabled);

private:
    void blitGrid();
    void highlightFocusedLink();
    void drawStatus();
    void scrollToFocusedLink();
    void putCell (int row, int col, unsigned cp, attr_t attrs, int pair);

    headless::TerminalPage page;

    int screenRows;
    int screenCols;
    int contentRows;        // screenRows minus the status line
    int topRow;
    std::string status;
};

Viewer::Viewer()
{
    screenRows = 0;
    screenCols = 0;
    contentRows = 0;
    topRow = 0;
}

void Viewer::setNetworkEnabled (int enabled)
{
    page.setNetworkEnabled (enabled);
}

void Viewer::putCell (int row, int col, unsigned cp, attr_t attrs, int pair)
{
    cchar_t cc;
    wchar_t wch[2];

    wch[0] = (wchar_t) cp;
    wch[1] = 0;

    if (setcchar (&cc, wch, attrs, (short) pair, 0) == OK)
        mvadd_wch (row, col, &cc);
}

void Viewer::blitGrid()
{
    erase();

    headless::CellGrid* grid = page.getGrid();

    int sr = 0;

    while (sr < contentRows)
    {
        const int gr = topRow + sr;
        int sc = 0;

        while (sc < screenCols)
        {
            headless::Cell* cell = grid->at (sc, gr);

            if (cell == 0)
                break;

            if (cell->ch == 0)
            {
                // Trailing half of a wide glyph, already emitted.
                sc = sc + 1;
                continue;
            }

            attr_t attrs = A_NORMAL;

            if ((cell->attrs & headless::CellAttrBold) != 0)
                attrs = attrs | A_BOLD;

            if ((cell->attrs & headless::CellAttrUnderline) != 0)
                attrs = attrs | A_UNDERLINE;

            // A_ITALIC is missing from some ncurses builds; dim stands in.
            if ((cell->attrs & headless::CellAttrItalic) != 0)
                attrs = attrs | A_DIM;

            int pair = 0;

            if (cell->fg != headless::kColorDefault)
                pair = pairFor (cell->fg);

            putCell (sr, sc, cell->ch, attrs, pair);

            sc = sc + headless::codepointColumns (cell->ch);
        }

        sr = sr + 1;
    }
}

void Viewer::highlightFocusedLink()
{
    const int focused = page.getFocusedLink();

    if (focused < 0)
        return;

    std::ownvector<headless::Link>* links = page.getLinks();

    if (focused >= (int) links->size())
        return;

    headless::Link& link = (*links)[focused];
    headless::CellGrid* grid = page.getGrid();

    int r = link.y;

    while (r < link.y + link.height)
    {
        const int screenRow = r - topRow;

        if (screenRow >= 0 && screenRow < contentRows)
        {
            int c = link.x;

            while (c < link.x + link.width)
            {
                headless::Cell* cell = grid->at (c, r);

                // Only cells that hold a glyph. An inline anchor's box is
                // taller than its text -- it takes in the line's leading --
                // so painting the whole box would highlight a blank row.
                if (cell != 0 && cell->ch != 32 && cell->ch != 0)
                    putCell (screenRow, c, cell->ch, A_REVERSE | A_BOLD, kHighlightPair);

                c = c + 1;
            }
        }

        r = r + 1;
    }
}

void Viewer::drawStatus()
{
    if (contentRows >= screenRows)
        return;

    std::string line;

    if (! status.empty())
    {
        line.assign (status.c_str());
    }
    else
    {
        const char* title = page.getTitle();

        if (title != 0 && title[0] != '\0')
            line.assign (title);
        else
            line.assign (page.getPath());

        const int focused = page.getFocusedLink();
        std::ownvector<headless::Link>* links = page.getLinks();

        if (focused >= 0 && focused < (int) links->size())
        {
            line.append ("  -> ");
            line.append ((*links)[focused].href.c_str());
        }
        else if (links->size() > 0)
        {
            line.append ("  (tab selects a link)");
        }
    }

    int c = 0;

    while (c < screenCols)
    {
        unsigned cp = 32;

        if (c < (int) line.size())
            cp = (unsigned) (unsigned char) line[c];

        putCell (contentRows, c, cp, A_REVERSE, 0);
        c = c + 1;
    }
}

void Viewer::scrollToFocusedLink()
{
    const int focused = page.getFocusedLink();

    if (focused < 0)
        return;

    std::ownvector<headless::Link>* links = page.getLinks();

    if (focused >= (int) links->size())
        return;

    headless::Link& link = (*links)[focused];

    if (link.y < topRow)
        topRow = link.y;

    if (link.y >= topRow + contentRows)
        topRow = link.y - contentRows + 1;
}

//==============================================================================

int Viewer::run (const char* path)
{
    // Reading the document from stdin would fight ncurses for the terminal,
    // so a piped document is read before curses starts.
    if (page.load (path) == 0)
    {
        fprintf (stderr, "error: %s\n", page.getError());
        return 1;
    }

    setlocale (LC_ALL, "");

    // With a piped document stdin is exhausted, so reopen the terminal for
    // key input rather than leaving getch at EOF.
    if (path == 0)
    {
        if (freopen ("/dev/tty", "r", stdin) == 0)
        {
            fprintf (stderr, "error: no terminal available for input\n");
            return 1;
        }
    }

    initscr();
    cbreak();
    noecho();
    keypad (stdscr, TRUE);
    curs_set (0);

    if (has_colors() != FALSE)
    {
        start_color();
        use_default_colors();

        if (COLOR_PAIRS > kHighlightPair)
            init_pair ((short) kHighlightPair, COLOR_BLACK, COLOR_YELLOW);
    }

    getmaxyx (stdscr, screenRows, screenCols);

    contentRows = screenRows - 1;

    if (contentRows < 1)
        contentRows = screenRows;

    // ncurses cannot draw pixels, so the grid keeps its [img] placeholders.
    page.getContainer()->setImagePlaceholders (1);
    page.layout (screenCols);

    int running = 1;

    while (running != 0)
    {
        const int docRows = page.getRows();
        int maxTop = docRows - contentRows;

        if (maxTop < 0)
            maxTop = 0;

        if (topRow > maxTop) topRow = maxTop;
        if (topRow < 0) topRow = 0;

        blitGrid();
        highlightFocusedLink();
        drawStatus();
        refresh();

        status.clear();

        const int key = getch();

        if (key == 'q' || key == 27)
        {
            running = 0;
        }
        else if (key == KEY_DOWN || key == 'j')
        {
            topRow = topRow + 1;
        }
        else if (key == KEY_UP || key == 'k')
        {
            topRow = topRow - 1;
        }
        else if (key == ' ' || key == KEY_NPAGE)
        {
            topRow = topRow + contentRows;
        }
        else if (key == 'b' || key == KEY_PPAGE)
        {
            topRow = topRow - contentRows;
        }
        else if (key == 'g' || key == KEY_HOME)
        {
            topRow = 0;
        }
        else if (key == 'G' || key == KEY_END)
        {
            topRow = maxTop;
        }
        else if (key == '\t' || key == 9 || key == 'n')
        {
            if (page.getFocusedLink() < 0)
                page.focusFirstLinkFrom (topRow);
            else
                page.moveFocus (1);

            scrollToFocusedLink();
        }
        else if (key == KEY_BTAB || key == 'p')
        {
            if (page.getFocusedLink() < 0)
                page.focusFirstLinkFrom (topRow);
            else
                page.moveFocus (-1);

            scrollToFocusedLink();
        }
        else if (key == KEY_BACKSPACE || key == 127 || key == 8)
        {
            if (page.canGoBack() != 0)
            {
                if (page.goBack (screenCols) != 0)
                    topRow = 0;
                else
                    status.assign (page.getError());
            }
            else
            {
                status.assign ("no page to go back to");
            }
        }
        else if (key == KEY_ENTER || key == '\r' || key == '\n' || key == 10 || key == 13)
        {
            if (page.getFocusedLink() < 0)
            {
                status.assign ("no link selected -- press tab");
            }
            else if (page.followFocusedLink (screenCols) != 0)
            {
                topRow = 0;
            }
            else
            {
                const char* err = page.getError();

                if (err != 0 && err[0] != '\0')
                    status.assign (err);
                else
                    status.assign ("could not follow that link");
            }
        }
        else if (key == KEY_RESIZE)
        {
            getmaxyx (stdscr, screenRows, screenCols);

            contentRows = screenRows - 1;

            if (contentRows < 1)
                contentRows = screenRows;

            page.layout (screenCols);
        }
    }

    endwin();
    return 0;
}

//==============================================================================

int main (int argc, char** argv)
{
    std::string inputPath;
    int allowNetwork = 0;
    int i = 1;

    while (i < argc)
    {
        const char* arg = argv[i];

        if (strcmp (arg, "--help") == 0)
        {
            printf ("Terminal browser for local HTML (ncurses).\n\n");
            printf ("Usage: %s [file.html]\n\n", argv[0]);
            printf ("Keys:\n");
            printf ("  arrows / j k   scroll         space / b    page\n");
            printf ("  g / G          top / bottom   tab / n / p  select link\n");
            printf ("  enter          follow link    backspace    back\n");
            printf ("  q / esc        quit\n\n");
            printf ("Pass a URL instead of a file to browse over http, with --net.\n");
            return 0;
        }
        else if (strcmp (arg, "--net") == 0)
        {
            allowNetwork = 1;
        }
        else if (arg[0] != '-' || strcmp (arg, "-") == 0)
        {
            inputPath.assign (arg);
        }

        i = i + 1;
    }

    Viewer viewer;

    viewer.setNetworkEnabled (allowNetwork);

    const char* path = 0;

    if (! inputPath.empty())
        path = inputPath.c_str();

    return viewer.run (path);
}
