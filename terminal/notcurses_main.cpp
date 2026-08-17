#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <notcurses/notcurses.h>

#include "terminal_page.h"

// Interactive browser on notcurses. Beyond the ncurses backend this adds
// inline images -- notcurses can draw real pixels -- and truecolor without
// the quantisation ncurses needs.

static const int kMaxImagePlanes = 32;

class Viewer
{
public:
    Viewer();

    int run (const char* path);
    void setNetworkEnabled (int enabled);

private:
    void blitGrid();
    void blitImages();
    void clearImagePlanes();
    void highlightFocusedLink();
    void drawStatus();
    void scrollToFocusedLink();
    void putCell (int row, int col, unsigned cp);

    struct notcurses* nc;
    struct ncplane* plane;
    headless::TerminalPage page;

    struct ncplane* imagePlanes[kMaxImagePlanes];
    struct ncvisual* imageVisuals[kMaxImagePlanes];
    int imagePlaneCount;

    int screenRows;
    int screenCols;
    int contentRows;            // screenRows minus the status line
    int topRow;
    std::string status;
};

Viewer::Viewer()
{
    nc = 0;
    plane = 0;
    imagePlaneCount = 0;
    screenRows = 0;
    screenCols = 0;
    contentRows = 0;
    topRow = 0;

    int i = 0;

    while (i < kMaxImagePlanes)
    {
        imagePlanes[i] = 0;
        imageVisuals[i] = 0;
        i = i + 1;
    }
}

void Viewer::setNetworkEnabled (int enabled)
{
    page.setNetworkEnabled (enabled);
}

void Viewer::putCell (int row, int col, unsigned cp)
{
    std::string one;
    headless::gridAppendUtf8 (&one, cp);

    char utf8[8];
    int len = (int) one.size();

    if (len > 7)
        len = 7;

    int k = 0;

    while (k < len)
    {
        utf8[k] = one[k];
        k = k + 1;
    }

    utf8[len] = '\0';

    ncplane_putstr_yx (plane, row, col, utf8);
}

//==============================================================================

void Viewer::clearImagePlanes()
{
    int i = 0;

    while (i < imagePlaneCount)
    {
        if (imagePlanes[i] != 0)
            ncplane_destroy (imagePlanes[i]);

        if (imageVisuals[i] != 0)
            ncvisual_destroy (imageVisuals[i]);

        imagePlanes[i] = 0;
        imageVisuals[i] = 0;
        i = i + 1;
    }

    imagePlaneCount = 0;
}

void Viewer::blitImages()
{
    clearImagePlanes();

    std::ownvector<headless::PageImage>* images = page.getImages();
    int i = 0;

    while (i < (int) images->size() && imagePlaneCount < kMaxImagePlanes)
    {
        headless::PageImage& img = (*images)[i];

        const int screenRow = img.y - topRow;

        // Skip anything scrolled off, including partially visible images: a
        // clipped plane would need its own crop, and half an image is worse
        // than the space it would have occupied.
        if (screenRow < 0 || screenRow + img.height > contentRows)
        {
            i = i + 1;
            continue;
        }

        struct ncvisual* visual = ncvisual_from_file (img.path.c_str());

        if (visual == 0)
        {
            // Unreadable or an unsupported format. Nothing is drawn and the
            // layout keeps the space, which is the right fallback.
            i = i + 1;
            continue;
        }

        // ncvisual_options carries no destination size: the target size is
        // the plane's. So make a plane of exactly the cell box layout
        // reserved and stretch the image into it.
        struct ncplane_options nopts;
        memset (&nopts, 0, sizeof (nopts));

        nopts.y = screenRow;
        nopts.x = img.x;
        nopts.rows = (unsigned) img.height;
        nopts.cols = (unsigned) img.width;

        struct ncplane* p = ncplane_create (plane, &nopts);

        if (p == 0)
        {
            ncvisual_destroy (visual);
            i = i + 1;
            continue;
        }

        struct ncvisual_options vopts;
        memset (&vopts, 0, sizeof (vopts));

        vopts.n = p;
        vopts.scaling = NCSCALE_STRETCH;
        vopts.blitter = NCBLIT_2x1;

        if (ncvisual_blit (nc, visual, &vopts) == 0)
        {
            ncplane_destroy (p);
            ncvisual_destroy (visual);
            i = i + 1;
            continue;
        }

        imagePlanes[imagePlaneCount] = p;
        imageVisuals[imagePlaneCount] = visual;
        imagePlaneCount = imagePlaneCount + 1;

        i = i + 1;
    }
}

//==============================================================================

void Viewer::blitGrid()
{
    ncplane_erase (plane);

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
                sc = sc + 1;
                continue;
            }

            uint16_t stylemask = 0;

            if ((cell->attrs & headless::CellAttrBold) != 0)
                stylemask = stylemask | NCSTYLE_BOLD;

            if ((cell->attrs & headless::CellAttrItalic) != 0)
                stylemask = stylemask | NCSTYLE_ITALIC;

            if ((cell->attrs & headless::CellAttrUnderline) != 0)
                stylemask = stylemask | NCSTYLE_UNDERLINE;

            ncplane_set_styles (plane, stylemask);

            if (cell->fg == headless::kColorDefault)
                ncplane_set_fg_default (plane);
            else
                ncplane_set_fg_rgb8 (plane, (cell->fg >> 24) & 255,
                                     (cell->fg >> 16) & 255, (cell->fg >> 8) & 255);

            if (cell->bg == headless::kColorDefault)
                ncplane_set_bg_default (plane);
            else
                ncplane_set_bg_rgb8 (plane, (cell->bg >> 24) & 255,
                                     (cell->bg >> 16) & 255, (cell->bg >> 8) & 255);

            putCell (sr, sc, cell->ch);

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
                {
                    ncplane_set_styles (plane, NCSTYLE_NONE);
                    ncplane_set_fg_rgb8 (plane, 0, 0, 0);
                    ncplane_set_bg_rgb8 (plane, 255, 220, 90);
                    putCell (screenRow, c, cell->ch);
                }

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

    ncplane_set_styles (plane, NCSTYLE_NONE);
    ncplane_set_fg_rgb8 (plane, 230, 230, 230);
    ncplane_set_bg_rgb8 (plane, 40, 40, 60);

    int c = 0;

    while (c < screenCols)
    {
        putCell (contentRows, c, 32);
        c = c + 1;
    }

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

    int k = 0;

    while (k < (int) line.size() && k < screenCols)
    {
        putCell (contentRows, k, (unsigned) (unsigned char) line[k]);
        k = k + 1;
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
    if (page.load (path) == 0)
    {
        fprintf (stderr, "error: %s\n", page.getError());
        return 1;
    }

    setlocale (LC_ALL, "");

    // A piped document leaves stdin at EOF, so reopen the terminal for keys.
    if (path == 0)
    {
        if (freopen ("/dev/tty", "r", stdin) == 0)
        {
            fprintf (stderr, "error: no terminal available for input\n");
            return 1;
        }
    }

    notcurses_options opts;
    memset (&opts, 0, sizeof (opts));
    opts.flags = NCOPTION_SUPPRESS_BANNERS;

    nc = notcurses_init (&opts, stdout);

    if (nc == 0)
    {
        fprintf (stderr, "error: could not initialise notcurses\n");
        return 1;
    }

    plane = notcurses_stdplane (nc);

    unsigned rowsU = 0;
    unsigned colsU = 0;
    ncplane_dim_yx (plane, &rowsU, &colsU);

    screenRows = (int) rowsU;
    screenCols = (int) colsU;
    contentRows = screenRows - 1;

    if (contentRows < 1)
        contentRows = screenRows;

    // notcurses draws the images, so the grid must not draw a placeholder
    // under them.
    page.getContainer()->setImagePlaceholders (0);
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
        blitImages();
        notcurses_render (nc);

        status.clear();

        ncinput ni;
        const uint32_t key = notcurses_get_blocking (nc, &ni);

        if (key == (uint32_t) -1)
        {
            running = 0;
        }
        else if (ni.evtype == NCTYPE_RELEASE)
        {
            // Press only, or every key would act twice.
        }
        else if (key == 'q' || key == NCKEY_ESC)
        {
            running = 0;
        }
        else if (key == NCKEY_DOWN || key == 'j')
        {
            topRow = topRow + 1;
        }
        else if (key == NCKEY_UP || key == 'k')
        {
            topRow = topRow - 1;
        }
        else if (key == ' ' || key == NCKEY_PGDOWN)
        {
            topRow = topRow + contentRows;
        }
        else if (key == 'b' || key == NCKEY_PGUP)
        {
            topRow = topRow - contentRows;
        }
        else if (key == 'g' || key == NCKEY_HOME)
        {
            topRow = 0;
        }
        else if (key == 'G' || key == NCKEY_END)
        {
            topRow = maxTop;
        }
        else if (key == NCKEY_TAB || key == 'n')
        {
            if (page.getFocusedLink() < 0)
                page.focusFirstLinkFrom (topRow);
            else
                page.moveFocus (1);

            scrollToFocusedLink();
        }
        else if (key == 'p')
        {
            if (page.getFocusedLink() < 0)
                page.focusFirstLinkFrom (topRow);
            else
                page.moveFocus (-1);

            scrollToFocusedLink();
        }
        else if (key == NCKEY_BACKSPACE)
        {
            if (page.canGoBack() != 0)
            {
                clearImagePlanes();

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
        else if (key == NCKEY_ENTER || key == '\r' || key == '\n')
        {
            if (page.getFocusedLink() < 0)
            {
                status.assign ("no link selected -- press tab");
            }
            else
            {
                // The planes point at the old document's images.
                clearImagePlanes();

                if (page.followFocusedLink (screenCols) != 0)
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
        }
        else if (key == NCKEY_RESIZE)
        {
            notcurses_refresh (nc, &rowsU, &colsU);
            ncplane_dim_yx (plane, &rowsU, &colsU);
            screenRows = (int) rowsU;
            screenCols = (int) colsU;
            contentRows = screenRows - 1;

            if (contentRows < 1)
                contentRows = screenRows;

            clearImagePlanes();
            page.layout (screenCols);
        }
    }

    clearImagePlanes();
    notcurses_stop (nc);
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
            printf ("Terminal browser for local HTML (notcurses).\n\n");
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
