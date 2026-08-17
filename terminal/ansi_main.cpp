#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "terminal_page.h"

// The dependency-free backend: writes the grid to stdout as ANSI escape
// codes. Nothing to link, works over a pipe, and therefore testable in CI --
// which is why the golden tests point at this one rather than at ncurses.

static int detectColumns (int fallback)
{
    const char* env = getenv ("COLUMNS");

    if (env != 0)
    {
        const int n = atoi (env);

        if (n > 0)
            return n;
    }

    return fallback;
}

int main (int argc, char** argv)
{
    int columns = 0;
    int color = -1;                 // -1 means auto
    int listLinks = 0;
    int allowNetwork = 0;
    std::string inputPath;

    int i = 1;

    while (i < argc)
    {
        const char* arg = argv[i];
        const int hasNext = (i + 1 < argc) ? 1 : 0;

        if (strcmp (arg, "--help") == 0)
        {
            printf ("Render an HTML document to the terminal.\n\n");
            printf ("Usage: %s [options] [file.html]\n\n", argv[0]);
            printf ("  -c, --cols N     Column count (default: terminal width, else 80)\n");
            printf ("      --color      Force colour output\n");
            printf ("      --no-color   Plain text, no escape codes\n");
            printf ("      --links      List the document's links instead of rendering\n");
            printf ("      --help       Show this message\n");
            return 0;
        }
        else if (strcmp (arg, "-c") == 0 || strcmp (arg, "--cols") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            columns = atoi (argv[i]);
        }
        else if (strcmp (arg, "--color") == 0)
        {
            color = 1;
        }
        else if (strcmp (arg, "--net") == 0)
        {
            allowNetwork = 1;
        }
        else if (strcmp (arg, "--links") == 0)
        {
            listLinks = 1;
        }
        else if (strcmp (arg, "--no-color") == 0)
        {
            color = 0;
        }
        else if (arg[0] == '-' && strcmp (arg, "-") != 0)
        {
            fprintf (stderr, "error: unknown option %s\n", arg);
            return 2;
        }
        else
        {
            inputPath.assign (arg);
        }

        i = i + 1;
    }

    if (columns <= 0)
        columns = detectColumns (80);

    // Auto: colour only when stdout is a terminal, so a redirect stays clean.
    if (color < 0)
        color = (isatty (1) != 0) ? 1 : 0;

    headless::TerminalPage page;

    page.setNetworkEnabled (allowNetwork);

    const char* path = 0;

    if (! inputPath.empty())
        path = inputPath.c_str();

    if (page.load (path) == 0)
    {
        fprintf (stderr, "error: %s\n", page.getError());
        return 1;
    }

    page.layout (columns);

    headless::CellGrid* grid = page.getGrid();

    if (grid->getCols() < 1)
    {
        fprintf (stderr, "error: %s\n", page.getError());
        return 1;
    }

    if (listLinks != 0)
    {
        std::ownvector<headless::Link>* links = page.getLinks();
        int k = 0;

        while (k < (int) links->size())
        {
            headless::Link& link = (*links)[k];
            printf ("[%d] (%d,%d %dx%d) %s -> %s\n", k,
                    link.x, link.y, link.width, link.height,
                    link.text.c_str(), link.href.c_str());
            k = k + 1;
        }

        return 0;
    }

    std::string out;
    grid->toAnsi (&out, color, page.getRows());

    fwrite (out.c_str(), 1, out.size(), stdout);

    return 0;
}
