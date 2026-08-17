#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../headless/context.h"
#include "../headless/crust_compat.h"
#include "../headless/links.h"
#include "../headless/url.h"
#include "gtk_container.h"

// The GTK front end. Everything below the window is shared: GtkContainer
// derives from CairoContainer, which derives from the headless Container, so
// layout, loading, fonts and painting are all inherited and this file is a
// window, a scroll position and an event loop.

namespace headless {

class Browser
{
public:
    Browser();

    /** Loads a path or url, lays it out, and records history. Returns 1 on
        success; on failure getError() explains and the current page stays.
     */
    int navigate (const char* target, int addToHistory);

    int goBack();
    int canGoBack();

    /** Re-lays out at a new width. */
    void relayout (int widthPx);

    /** Paints into a cairo context. */
    void render (cairo_t* cr, int width, int height);

    /** Returns 1 when the click landed on a link that was followed. */
    int click (int docX, int docY);

    /** Returns 1 when the cursor should be a hand. */
    int hover (int docX, int docY);

    int getDocumentHeight();
    const char* getTitle();
    const char* getCurrentUrl();
    const char* getError();

    void setNetworkEnabled (int enabled);

    /** Writes the current page to a png, for checking without a display. */
    int screenshot (const char* path, int width);

    GtkContainer container;

private:
    Context context;
    litehtml::document::ptr doc;
    std::string html;
    std::string currentUrl;
    std::string error;
    Url documentUrl;
    int layoutWidth;
    std::ownvector<std::string> history;
};

Browser::Browser()
{
    layoutWidth = 800;
}

void Browser::setNetworkEnabled (int enabled)
{
    container.getLoader()->setNetworkEnabled (enabled);
}

static int readWholeFile (const char* path, std::string* out)
{
    FILE* f = fopen (path, "rb");

    if (f == 0)
        return 0;

    char buffer[8192];
    size_t n = fread (buffer, 1, 8192, f);

    while (n > 0)
    {
        int k = 0;

        while (k < (int) n)
        {
            out->push_back (buffer[k]);
            k = k + 1;
        }

        n = fread (buffer, 1, 8192, f);
    }

    fclose (f);
    return 1;
}

int Browser::navigate (const char* target, int addToHistory)
{
    if (target == 0 || target[0] == '\0')
        return 0;

    error.clear();

    std::string body;
    Url url;
    int loaded = 0;

    if (parseUrl (target, &url) != 0 && url.isRemote() != 0)
    {
        Url finalUrl;

        if (container.getLoader()->fetch (&url, &body, &finalUrl) == 0)
        {
            error.assign (container.getLoader()->getError());
            return 0;
        }

        // Relative links resolve against where we landed, not where we asked.
        url = finalUrl;
        loaded = 1;
    }
    else
    {
        const char* path = target;

        if (strncmp (target, "file://", 7) == 0)
            path = target + 7;

        if (readWholeFile (path, &body) == 0)
        {
            error.assign ("cannot open ");
            error.append (path);
            return 0;
        }

        urlFromPath (path, &url);
        loaded = 1;
    }

    if (loaded == 0)
        return 0;

    if (addToHistory != 0 && ! currentUrl.empty())
        history.push_back (currentUrl);

    html = body;
    documentUrl = url;

    std::string text;
    documentUrl.toString (&text);
    currentUrl.assign (text.c_str());

    container.clearImages();
    container.setDocumentUrl (&documentUrl);

    relayout (layoutWidth);
    return 1;
}

int Browser::canGoBack()
{
    return (history.size() > 0) ? 1 : 0;
}

int Browser::goBack()
{
    if (history.size() == 0)
        return 0;

    const int last = (int) history.size() - 1;

    std::string target;
    target.assign (history[last].c_str());
    history.pop_back();

    return navigate (target.c_str(), 0);
}

void Browser::relayout (int widthPx)
{
    if (widthPx < 1)
        widthPx = 1;

    layoutWidth = widthPx;

    if (html.empty())
        return;

    container.setViewport (widthPx, 600);
    container.clearDrawCommands();

    doc = litehtml::document::createFromUTF8 (html.c_str(), &container, &context);

    if (! doc)
    {
        error.assign ("failed to parse document");
        return;
    }

    doc->render (widthPx);

    int h = doc->height();

    if (h < 1)
        h = 1;

    container.setViewport (widthPx, h);
}

int Browser::getDocumentHeight()
{
    if (! doc)
        return 1;

    const int h = doc->height();
    return (h > 0) ? h : 1;
}

const char* Browser::getTitle() { return container.getCaption()->c_str(); }
const char* Browser::getCurrentUrl() { return currentUrl.c_str(); }
const char* Browser::getError() { return error.c_str(); }

void Browser::render (cairo_t* cr, int width, int height)
{
    if (! doc || cr == 0)
        return;

    // White page under everything, since a document need not paint one.
    cairo_save (cr);
    cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
    cairo_paint (cr);
    cairo_restore (cr);

    container.setTarget (cr);
    container.clearDrawCommands();

    litehtml::position clip;
    clip.x = 0;
    clip.y = 0;
    clip.width = width;
    clip.height = height;

    doc->draw (0, 0, 0, &clip);

    container.setTarget (0);
}

int Browser::click (int docX, int docY)
{
    if (! doc)
        return 0;

    litehtml::position::vector redraw;

    doc->on_lbutton_down (docX, docY, docX, docY, redraw);

    const int before = (int) container.getClickedAnchors()->size();

    doc->on_lbutton_up (docX, docY, docX, docY, redraw);

    std::ownvector<std::string>* clicked = container.getClickedAnchors();

    if ((int) clicked->size() <= before)
        return 0;

    // litehtml reports the href it was given, so it still has to be resolved
    // against the document url.
    const int last = (int) clicked->size() - 1;

    std::string href;
    href.assign ((*clicked)[last].c_str());

    Url resolved;

    if (resolveUrl (&documentUrl, href.c_str(), &resolved) == 0)
    {
        error.assign ("cannot follow ");
        error.append (href.c_str());
        return 0;
    }

    std::string target;
    resolved.toString (&target);

    return navigate (target.c_str(), 1);
}

int Browser::hover (int docX, int docY)
{
    if (! doc)
        return 0;

    litehtml::position::vector redraw;
    doc->on_mouse_over (docX, docY, docX, docY, redraw);

    return (strcmp (container.getCursorName(), "pointer") == 0) ? 1 : 0;
}

int Browser::screenshot (const char* path, int width)
{
    relayout (width);

    if (! doc)
        return 0;

    const int height = getDocumentHeight();

    cairo_surface_t* surface =
        cairo_image_surface_create (CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create (surface);

    render (cr, width, height);
    cairo_surface_flush (surface);

    const cairo_status_t status = cairo_surface_write_to_png (surface, path);

    cairo_destroy (cr);
    cairo_surface_destroy (surface);

    if (status != CAIRO_STATUS_SUCCESS)
    {
        error.assign ("could not write ");
        error.append (path);
        return 0;
    }

    return 1;
}

} // namespace headless

//==============================================================================
// Window
//==============================================================================

/** Everything the GTK callbacks need. Callbacks are plain functions, since
    the subset has no capturing lambdas to hand to g_signal_connect.
 */
class Window
{
public:
    Window();

    headless::Browser browser;

    GtkWidget* window;
    GtkWidget* canvas;
    GtkWidget* entry;
    GtkWidget* backButton;
    GtkWidget* status;

    int lastWidth;
    int handContext;
};

Window::Window()
{
    window = 0;
    canvas = 0;
    entry = 0;
    backButton = 0;
    status = 0;
    lastWidth = 0;
    handContext = 0;
}

static void refreshChrome (Window* w)
{
    gtk_entry_set_text (GTK_ENTRY (w->entry), w->browser.getCurrentUrl());
    gtk_widget_set_sensitive (w->backButton,
                              w->browser.canGoBack() != 0 ? TRUE : FALSE);

    const char* title = w->browser.getTitle();

    if (title != 0 && title[0] != '\0')
        gtk_window_set_title (GTK_WINDOW (w->window), title);
    else
        gtk_window_set_title (GTK_WINDOW (w->window), "litehtml");

    const char* err = w->browser.getError();
    gtk_label_set_text (GTK_LABEL (w->status), (err != 0) ? err : "");

    gtk_widget_set_size_request (w->canvas, -1, w->browser.getDocumentHeight());
    gtk_widget_queue_draw (w->canvas);
}

static gboolean onDraw (GtkWidget* widget, cairo_t* cr, gpointer data)
{
    Window* w = (Window*) data;

    GtkAllocation alloc;
    gtk_widget_get_allocation (widget, &alloc);

    w->browser.render (cr, alloc.width, alloc.height);
    return FALSE;
}

static void onSizeAllocate (GtkWidget* widget, GdkRectangle* alloc, gpointer data)
{
    Window* w = (Window*) data;

    if (alloc->width == w->lastWidth || alloc->width < 1)
        return;

    // Reflow only when the width actually changed; a height change is just
    // the scrolled window catching up with the document.
    w->lastWidth = alloc->width;
    w->browser.relayout (alloc->width);

    gtk_widget_set_size_request (w->canvas, -1, w->browser.getDocumentHeight());
    gtk_widget_queue_draw (w->canvas);
}

static gboolean onButtonPress (GtkWidget* widget, GdkEventButton* event, gpointer data)
{
    Window* w = (Window*) data;

    if (event->button != 1)
        return FALSE;

    if (w->browser.click ((int) event->x, (int) event->y) != 0)
        refreshChrome (w);
    else
        gtk_label_set_text (GTK_LABEL (w->status), w->browser.getError());

    return TRUE;
}

static gboolean onMotion (GtkWidget* widget, GdkEventMotion* event, gpointer data)
{
    Window* w = (Window*) data;

    const int wantHand = w->browser.hover ((int) event->x, (int) event->y);

    if (wantHand != w->handContext)
    {
        w->handContext = wantHand;

        GdkWindow* gdkWindow = gtk_widget_get_window (widget);

        if (gdkWindow != 0)
        {
            GdkCursor* cursor = gdk_cursor_new_from_name (
                gdk_window_get_display (gdkWindow),
                (wantHand != 0) ? "pointer" : "default");

            gdk_window_set_cursor (gdkWindow, cursor);

            if (cursor != 0)
                g_object_unref (cursor);
        }

        gtk_widget_queue_draw (widget);
    }

    return TRUE;
}

static void onEntryActivate (GtkEntry* entry, gpointer data)
{
    Window* w = (Window*) data;
    const char* text = gtk_entry_get_text (entry);

    if (w->browser.navigate (text, 1) != 0)
        refreshChrome (w);
    else
        gtk_label_set_text (GTK_LABEL (w->status), w->browser.getError());
}

static void onBackClicked (GtkButton* button, gpointer data)
{
    Window* w = (Window*) data;

    if (w->browser.goBack() != 0)
        refreshChrome (w);
}

static gboolean onKeyPress (GtkWidget* widget, GdkEventKey* event, gpointer data)
{
    Window* w = (Window*) data;

    if (event->keyval == GDK_KEY_BackSpace)
    {
        if (w->browser.goBack() != 0)
            refreshChrome (w);

        return TRUE;
    }

    if ((event->state & GDK_MOD1_MASK) != 0 && event->keyval == GDK_KEY_Left)
    {
        if (w->browser.goBack() != 0)
            refreshChrome (w);

        return TRUE;
    }

    if ((event->state & GDK_CONTROL_MASK) != 0 && event->keyval == GDK_KEY_l)
    {
        gtk_widget_grab_focus (w->entry);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_q && (event->state & GDK_CONTROL_MASK) != 0)
    {
        gtk_main_quit();
        return TRUE;
    }

    return FALSE;
}

//==============================================================================

int main (int argc, char** argv)
{
    std::string target;
    std::string screenshotPath;
    int allowNetwork = 0;
    int clickX = -1;
    int clickY = -1;
    int width = 900;
    int height = 700;

    int i = 1;

    while (i < argc)
    {
        const char* arg = argv[i];
        const int hasNext = (i + 1 < argc) ? 1 : 0;

        if (strcmp (arg, "--help") == 0)
        {
            printf ("A small browser on litehtml and GTK.\n\n");
            printf ("Usage: %s [options] [file.html | url]\n\n", argv[0]);
            printf ("  --net              Allow http/https fetching (off by default)\n");
            printf ("  -w, --width N      Window width (default 900)\n");
            printf ("  --screenshot FILE  Render to a png and exit, no window needed\n");
            printf ("  --click X,Y        Follow the link at X,Y first (with --screenshot)\n");
            printf ("  --help             Show this message\n\n");
            printf ("Click a link to follow it; backspace or alt+left goes back,\n");
            printf ("ctrl+l focuses the address bar, ctrl+q quits.\n");
            return 0;
        }
        else if (strcmp (arg, "--net") == 0)
        {
            allowNetwork = 1;
        }
        else if (strcmp (arg, "-w") == 0 || strcmp (arg, "--width") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s needs a value\n", arg); return 2; }
            i = i + 1;
            width = atoi (argv[i]);
        }
        else if (strcmp (arg, "--click") == 0)
        {
            // Follow a link at a document coordinate before rendering. This
            // exists so navigation can be checked without a display, which is
            // otherwise the one part of this front end that CI cannot reach.
            if (hasNext == 0) { fprintf (stderr, "error: %s needs X,Y\n", arg); return 2; }
            i = i + 1;

            const char* comma = strchr (argv[i], ',');

            if (comma == 0)
            {
                fprintf (stderr, "error: --click expects X,Y\n");
                return 2;
            }

            clickX = atoi (argv[i]);
            clickY = atoi (comma + 1);
        }
        else if (strcmp (arg, "--screenshot") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s needs a value\n", arg); return 2; }
            i = i + 1;
            screenshotPath.assign (argv[i]);
        }
        else if (arg[0] == '-' && strcmp (arg, "-") != 0)
        {
            fprintf (stderr, "error: unknown option %s\n", arg);
            return 2;
        }
        else
        {
            target.assign (arg);
        }

        i = i + 1;
    }

    if (target.empty())
    {
        fprintf (stderr, "error: nothing to load (try --help)\n");
        return 2;
    }

    // Screenshot mode renders offscreen, so it needs no display at all --
    // which is what makes the GTK path checkable in CI.
    if (! screenshotPath.empty())
    {
        gtk_init_check (&argc, &argv);

        headless::Browser browser;
        browser.setNetworkEnabled (allowNetwork);

        if (browser.navigate (target.c_str(), 0) == 0)
        {
            fprintf (stderr, "error: %s\n", browser.getError());
            return 1;
        }

        if (clickX >= 0 && clickY >= 0)
        {
            browser.relayout (width);

            if (browser.click (clickX, clickY) == 0)
            {
                const char* err = browser.getError();
                fprintf (stderr, "error: no link followed at %d,%d%s%s\n",
                         clickX, clickY,
                         (err != 0 && err[0] != '\0') ? ": " : "",
                         (err != 0) ? err : "");
                return 1;
            }

            printf ("followed to %s\n", browser.getCurrentUrl());
        }

        if (browser.screenshot (screenshotPath.c_str(), width) == 0)
        {
            fprintf (stderr, "error: %s\n", browser.getError());
            return 1;
        }

        printf ("wrote %s (%dx%d)\n", screenshotPath.c_str(),
                width, browser.getDocumentHeight());
        return 0;
    }

    gtk_init (&argc, &argv);

    Window w;
    w.browser.setNetworkEnabled (allowNetwork);

    if (w.browser.navigate (target.c_str(), 0) == 0)
        fprintf (stderr, "warning: %s\n", w.browser.getError());

    w.window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (w.window), width, height);
    gtk_window_set_title (GTK_WINDOW (w.window), "litehtml");

    GtkWidget* vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add (GTK_CONTAINER (w.window), vbox);

    GtkWidget* bar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_set_border_width (GTK_CONTAINER (bar), 4);
    gtk_box_pack_start (GTK_BOX (vbox), bar, FALSE, FALSE, 0);

    w.backButton = gtk_button_new_with_label ("Back");
    gtk_widget_set_sensitive (w.backButton, FALSE);
    gtk_box_pack_start (GTK_BOX (bar), w.backButton, FALSE, FALSE, 0);

    w.entry = gtk_entry_new();
    gtk_box_pack_start (GTK_BOX (bar), w.entry, TRUE, TRUE, 0);

    GtkWidget* scrolled = gtk_scrolled_window_new (0, 0);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start (GTK_BOX (vbox), scrolled, TRUE, TRUE, 0);

    w.canvas = gtk_drawing_area_new();
    gtk_widget_add_events (w.canvas,
                           GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
                           | GDK_POINTER_MOTION_MASK);
    gtk_container_add (GTK_CONTAINER (scrolled), w.canvas);

    w.status = gtk_label_new ("");
    gtk_label_set_xalign (GTK_LABEL (w.status), 0.0f);
    gtk_box_pack_start (GTK_BOX (vbox), w.status, FALSE, FALSE, 2);

    g_signal_connect (w.window, "destroy", G_CALLBACK (gtk_main_quit), 0);
    g_signal_connect (w.window, "key-press-event", G_CALLBACK (onKeyPress), &w);
    g_signal_connect (w.canvas, "draw", G_CALLBACK (onDraw), &w);
    g_signal_connect (w.canvas, "size-allocate", G_CALLBACK (onSizeAllocate), &w);
    g_signal_connect (w.canvas, "button-press-event", G_CALLBACK (onButtonPress), &w);
    g_signal_connect (w.canvas, "motion-notify-event", G_CALLBACK (onMotion), &w);
    g_signal_connect (w.entry, "activate", G_CALLBACK (onEntryActivate), &w);
    g_signal_connect (w.backButton, "clicked", G_CALLBACK (onBackClicked), &w);

    refreshChrome (&w);

    gtk_widget_show_all (w.window);
    gtk_main();

    return 0;
}
