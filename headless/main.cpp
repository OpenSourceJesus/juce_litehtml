#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "container.h"
#include "context.h"
#include "crust_compat.h"
#include "dump.h"
#include "url.h"

// No iostreams: the subset refuses the stream operators, so this uses stdio.

static void printUsage (const char* argv0)
{
    printf ("Headless litehtml renderer -- no JUCE, no GTK, no window system.\n\n");
    printf ("Usage: %s [options] [file.html]\n\n", argv0);
    printf ("Reads HTML from a file, a URL, or stdin when neither is given (or\n");
    printf ("when the file is '-'), lays it out, and reports the result as text.\n\n");
    printf ("Options:\n");
    printf ("  -w, --width N        Viewport width in pixels (default 800)\n");
    printf ("  -h, --height N       Viewport height in pixels (default 600)\n");
    printf ("  -m, --mode MODE      Output mode (default text). One of:\n");
    printf ("                         text   reconstructed page text\n");
    printf ("                         tree   laid-out element tree with box geometry\n");
    printf ("                         draw   recorded display list in paint order\n");
    printf ("                         ascii  coarse ASCII rendering\n");
    printf ("                         stats  summary counters only\n");
    printf ("                         all    every mode above, with headings\n");
    printf ("  -b, --base DIR       Base directory for relative css/script urls\n");
    printf ("      --font-name NAME Default font family (default sans-serif)\n");
    printf ("      --font-size N    Default font size in pixels (default 16)\n");
    printf ("      --image-size WxH Size reported for images (default 0x0)\n");
    printf ("      --net            Allow http/https fetching (off by default)\n");
    printf ("      --help           Show this message\n");
}

/** Reads a whole stream into a string. */
static void readAll (FILE* in, std::string* out)
{
    char buffer[4096];
    size_t n = fread (buffer, 1, 4095, in);

    while (n > 0)
    {
        buffer[n] = '\0';
        out->append (buffer);
        n = fread (buffer, 1, 4095, in);
    }
}

/** Parses "WxH". Returns 1 on success. */
static int parseSize (const char* s, int* w, int* h)
{
    const char* x = strchr (s, 'x');

    if (x == 0)
        return 0;

    *w = atoi (s);
    *h = atoi (x + 1);
    return 1;
}

static void emitMode (const char* which,
                      headless::Container* container,
                      litehtml::document::ptr doc,
                      int width,
                      int withHeading)
{
    if (withHeading != 0)
        printf ("== %s ==\n", which);

    std::string out;

    if (strcmp (which, "text") == 0)
    {
        headless::dumpText (container, &out);
    }
    else if (strcmp (which, "tree") == 0)
    {
        headless::dumpLayoutTree (doc, &out);
    }
    else if (strcmp (which, "draw") == 0)
    {
        headless::dumpDisplayList (container, &out);
    }
    else if (strcmp (which, "ascii") == 0)
    {
        headless::dumpAscii (container, width, doc->height(), &out);
    }
    else if (strcmp (which, "stats") == 0)
    {
        printf ("title:        %s\n", container->getCaption()->c_str());
        printf ("size:         %dx%d\n", doc->width(), doc->height());
        printf ("fonts:        %d\n", container->getFontsCreated());
        printf ("draw calls:   %d\n", (int) container->getDrawCommands()->size());
        printf ("images:       %d\n", (int) container->getRequestedImages()->size());
        printf ("scripts:      %d\n", (int) container->getRequestedScripts()->size());
    }

    if (! out.empty())
        printf ("%s", out.c_str());

    if (withHeading != 0)
        printf ("\n");
}

int main (int argc, char** argv)
{
    int width = 800;
    int height = 600;
    int imageW = 0;
    int imageH = 0;
    int fontSize = 16;

    std::string mode;
    std::string baseDir;
    std::string fontName;
    std::string inputPath;
    int allowNetwork = 0;

    mode.assign ("text");
    fontName.assign ("sans-serif");

    int i = 1;

    while (i < argc)
    {
        const char* arg = argv[i];
        const int hasNext = (i + 1 < argc) ? 1 : 0;

        if (strcmp (arg, "--help") == 0)
        {
            printUsage (argv[0]);
            return 0;
        }
        else if (strcmp (arg, "-w") == 0 || strcmp (arg, "--width") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            width = atoi (argv[i]);
        }
        else if (strcmp (arg, "-h") == 0 || strcmp (arg, "--height") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            height = atoi (argv[i]);
        }
        else if (strcmp (arg, "-m") == 0 || strcmp (arg, "--mode") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            mode.assign (argv[i]);
        }
        else if (strcmp (arg, "-b") == 0 || strcmp (arg, "--base") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            baseDir.assign (argv[i]);
        }
        else if (strcmp (arg, "--font-name") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            fontName.assign (argv[i]);
        }
        else if (strcmp (arg, "--font-size") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            fontSize = atoi (argv[i]);
        }
        else if (strcmp (arg, "--image-size") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;

            if (parseSize (argv[i], &imageW, &imageH) == 0)
            {
                fprintf (stderr, "error: --image-size expects WxH, e.g. 64x64\n");
                return 2;
            }
        }
        else if (strcmp (arg, "--net") == 0)
        {
            allowNetwork = 1;
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

    if (width <= 0 || height <= 0)
    {
        fprintf (stderr, "error: width and height must be positive\n");
        return 2;
    }

    // Read the document. A URL goes through the loader; anything else is a
    // path or stdin, exactly as before.
    headless::Context context;
    headless::Container container;

    container.getLoader()->setNetworkEnabled (allowNetwork);

    std::string html;
    headless::Url docUrl;
    int haveUrl = 0;

    if (! inputPath.empty() && headless::parseUrl (inputPath.c_str(), &docUrl) != 0)
    {
        headless::Url finalUrl;

        if (container.getLoader()->fetch (&docUrl, &html, &finalUrl) == 0)
        {
            fprintf (stderr, "error: %s\n", container.getLoader()->getError());
            return 1;
        }

        // Relative references resolve against where we ended up, not where
        // we asked, which is what makes a redirect behave.
        container.setDocumentUrl (&finalUrl);
        haveUrl = 1;
    }
    else if (inputPath.empty() || strcmp (inputPath.c_str(), "-") == 0)
    {
        readAll (stdin, &html);
    }
    else
    {
        FILE* in = fopen (inputPath.c_str(), "rb");

        if (in == 0)
        {
            fprintf (stderr, "error: cannot open %s\n", inputPath.c_str());
            return 1;
        }

        readAll (in, &html);
        fclose (in);

        // Default the base directory to the document's own directory.
        if (baseDir.empty())
        {
            const char* path = inputPath.c_str();
            const char* slash = strrchr (path, '/');

            if (slash == 0)
            {
                baseDir.assign (".");
            }
            else
            {
                int n = (int) (slash - path);
                int k = 0;

                while (k < n)
                {
                    baseDir.push_back (path[k]);
                    k = k + 1;
                }
            }
        }
    }

    // Lay it out.
    container.setViewport (width, height);

    if (haveUrl == 0)
        container.setBaseDirectory (baseDir.c_str());
    container.setDefaultFontName (fontName.c_str());
    container.setDefaultFontSize (fontSize);
    container.setDefaultImageSize (imageW, imageH);

    litehtml::document::ptr doc =
        litehtml::document::createFromUTF8 (html.c_str(), &container, &context);

    if (! doc)
    {
        fprintf (stderr, "error: failed to parse document\n");
        return 1;
    }

    doc->render (width);

    litehtml::position clip;
    clip.x = 0;
    clip.y = 0;
    clip.width = width;
    clip.height = doc->height();

    doc->draw (0, 0, 0, &clip);

    // Report.
    const char* m = mode.c_str();

    if (strcmp (m, "all") == 0)
    {
        emitMode ("stats", &container, doc, width, 1);
        emitMode ("tree",  &container, doc, width, 1);
        emitMode ("text",  &container, doc, width, 1);
        emitMode ("ascii", &container, doc, width, 1);
        emitMode ("draw",  &container, doc, width, 1);
    }
    else if (strcmp (m, "text") == 0 || strcmp (m, "tree") == 0
             || strcmp (m, "draw") == 0 || strcmp (m, "ascii") == 0
             || strcmp (m, "stats") == 0)
    {
        emitMode (m, &container, doc, width, 0);
    }
    else
    {
        fprintf (stderr, "error: unknown mode '%s'\n", m);
        return 2;
    }

    return 0;
}
