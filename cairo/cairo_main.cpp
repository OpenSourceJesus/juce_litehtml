#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../headless/context.h"
#include "../headless/crust_compat.h"
#include "cairo_container.h"

// Renders a document to a PNG with no window system involved. This is the
// same container GTK uses; GTK only supplies a different cairo_t.

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

int main (int argc, char** argv)
{
    int width = 800;
    std::string inputPath;
    std::string outputPath;

    outputPath.assign ("out.png");

    int i = 1;

    while (i < argc)
    {
        const char* arg = argv[i];
        const int hasNext = (i + 1 < argc) ? 1 : 0;

        if (strcmp (arg, "--help") == 0)
        {
            printf ("Render an HTML document to a PNG using cairo.\n\n");
            printf ("Usage: %s [options] [file.html]\n\n", argv[0]);
            printf ("  -w, --width N    Page width in pixels (default 800)\n");
            printf ("  -o, --out FILE   Output PNG path (default out.png)\n");
            printf ("      --help       Show this message\n");
            return 0;
        }
        else if (strcmp (arg, "-w") == 0 || strcmp (arg, "--width") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            width = atoi (argv[i]);
        }
        else if (strcmp (arg, "-o") == 0 || strcmp (arg, "--out") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s requires a value\n", arg); return 2; }
            i = i + 1;
            outputPath.assign (argv[i]);
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

    if (width <= 0)
    {
        fprintf (stderr, "error: width must be positive\n");
        return 2;
    }

    std::string html;
    std::string baseDir;

    if (inputPath.empty() || strcmp (inputPath.c_str(), "-") == 0)
    {
        readAll (stdin, &html);
        baseDir.assign (".");
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

    headless::Context context;
    headless::CairoContainer container;

    container.setViewport (width, 600);
    container.setBaseDirectory (baseDir.c_str());

    litehtml::document::ptr doc =
        litehtml::document::createFromUTF8 (html.c_str(), &container, &context);

    if (! doc)
    {
        fprintf (stderr, "error: failed to parse document\n");
        return 1;
    }

    // Render once to learn the height, then draw onto a surface that size.
    doc->render (width);

    int height = doc->height();

    if (height < 1)
        height = 1;

    container.setViewport (width, height);

    cairo_surface_t* surface =
        cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create (surface);

    // White page.
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint (cr);

    container.setTarget (cr);

    litehtml::position clip;
    clip.x = 0;
    clip.y = 0;
    clip.width = width;
    clip.height = height;

    doc->draw (0, 0, 0, &clip);

    cairo_surface_flush (surface);

    const cairo_status_t status =
        cairo_surface_write_to_png (surface, outputPath.c_str());

    container.setTarget (0);
    cairo_destroy (cr);
    cairo_surface_destroy (surface);

    if (status != CAIRO_STATUS_SUCCESS)
    {
        fprintf (stderr, "error: could not write %s: %s\n",
                 outputPath.c_str(), cairo_status_to_string (status));
        return 1;
    }

    printf ("wrote %s (%dx%d, %d draw calls)\n",
            outputPath.c_str(), width, height,
            (int) container.getDrawCommands()->size());

    return 0;
}
