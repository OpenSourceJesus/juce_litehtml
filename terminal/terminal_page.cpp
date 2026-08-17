#include "terminal_page.h"

#include <stdio.h>
#include <string.h>

namespace headless {

PageImage::PageImage()
{
    x = 0;
    y = 0;
    width = 0;
    height = 0;
}

TerminalPage::TerminalPage()
{
    rows = 0;
    focusedLink = -1;
    isRemoteDoc = 0;
}

TerminalPage::~TerminalPage()
{
}

static void readAllStream (FILE* in, std::string* out)
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

void TerminalPage::setNetworkEnabled (int enabled)
{
    container.getLoader()->setNetworkEnabled (enabled);
}

int TerminalPage::load (const char* path)
{
    html.clear();
    error.clear();

    // A URL is fetched through the loader; everything else stays a path.
    if (path != 0 && parseUrl (path, &documentUrl) != 0 && documentUrl.isRemote() != 0)
    {
        Url finalUrl;

        if (container.getLoader()->fetch (&documentUrl, &html, &finalUrl) == 0)
        {
            error.assign (container.getLoader()->getError());
            return 0;
        }

        // Relative references resolve against where we landed, not where we
        // asked, so a redirect does not break the page's own links.
        documentUrl = finalUrl;
        isRemoteDoc = 1;

        std::string text;
        documentUrl.toString (&text);
        currentPath.assign (text.c_str());

        return 1;
    }

    isRemoteDoc = 0;

    if (path == 0 || strcmp (path, "-") == 0)
    {
        readAllStream (stdin, &html);
        baseDir.assign (".");
        currentPath.clear();
        return 1;
    }

    currentPath.assign (path);

    FILE* in = fopen (path, "rb");

    if (in == 0)
    {
        error.assign ("cannot open ");
        error.append (path);
        return 0;
    }

    readAllStream (in, &html);
    fclose (in);

    const char* slash = strrchr (path, '/');

    if (slash == 0)
    {
        baseDir.assign (".");
    }
    else
    {
        baseDir.clear();
        const int n = (int) (slash - path);
        int k = 0;

        while (k < n)
        {
            baseDir.push_back (path[k]);
            k = k + 1;
        }
    }

    return 1;
}

void TerminalPage::layout (int columns)
{
    if (columns < 1)
        columns = 1;

    if (isRemoteDoc != 0)
        container.setDocumentUrl (&documentUrl);
    else
        container.setBaseDirectory (baseDir.c_str());

    container.setColumns (columns);
    container.clearDrawCommands();

    const int widthPx = columns * container.getCellW();

    doc = litehtml::document::createFromUTF8 (html.c_str(), &container, &context);

    if (! doc)
    {
        error.assign ("failed to parse document");
        return;
    }

    doc->render (widthPx);

    int heightPx = doc->height();

    if (heightPx < 1)
        heightPx = 1;

    container.setViewport (widthPx, heightPx);

    litehtml::position clip;
    clip.x = 0;
    clip.y = 0;
    clip.width = widthPx;
    clip.height = heightPx;

    doc->draw (0, 0, 0, &clip);

    container.renderToGrid (&grid, heightPx);

    // Trim trailing blank rows so scrolling does not run off into nothing.
    const int last = grid.lastUsedRow();
    rows = (last >= 0) ? (last + 1) : 0;

    refreshLinks();
    refreshImages();
}

void TerminalPage::refreshLinks()
{
    collectLinks (doc, &links);

    // Convert pixel boxes to cells once, here, so the backends never have to
    // know the cell size.
    const int cw = container.getCellW();
    const int ch = container.getCellH();

    int i = 0;

    while (i < (int) links.size())
    {
        Link& link = links[i];

        const int x0 = link.x / cw;
        const int y0 = link.y / ch;
        const int x1 = (link.x + link.width + cw - 1) / cw;
        const int y1 = (link.y + link.height + ch - 1) / ch;

        link.x = x0;
        link.y = y0;
        link.width = x1 - x0;
        link.height = y1 - y0;

        if (link.width < 1) link.width = 1;
        if (link.height < 1) link.height = 1;

        i = i + 1;
    }

    focusedLink = -1;
}

void TerminalPage::refreshImages()
{
    images.clear();

    const int cw = container.getCellW();
    const int ch = container.getCellH();

    std::ownvector<DrawCommand>* commands = container.getDrawCommands();
    int i = 0;

    while (i < (int) commands->size())
    {
        DrawCommand& cmd = (*commands)[i];

        // An <img> reaches the container as a background carrying an image
        // url; a CSS background does too, and both are worth drawing.
        if (cmd.type == DrawTypeBackground && ! cmd.text.empty())
        {
            std::string path;

            if (container.resolveImagePath (cmd.text.c_str(), &path) != 0)
            {
                PageImage img;
                img.path.assign (path.c_str());
                img.x = cmd.x / cw;
                img.y = cmd.y / ch;
                img.width = (cmd.width + cw - 1) / cw;
                img.height = (cmd.height + ch - 1) / ch;

                if (img.width > 0 && img.height > 0)
                    images.push_back (img);
            }
        }

        i = i + 1;
    }
}

std::ownvector<PageImage>* TerminalPage::getImages() { return &images; }

std::ownvector<Link>* TerminalPage::getLinks() { return &links; }
int TerminalPage::getFocusedLink() { return focusedLink; }

void TerminalPage::setFocusedLink (int index)
{
    if (links.size() == 0)
    {
        focusedLink = -1;
        return;
    }

    if (index < 0 || index >= (int) links.size())
        return;

    focusedLink = index;
}

void TerminalPage::moveFocus (int delta)
{
    const int n = (int) links.size();

    if (n == 0)
    {
        focusedLink = -1;
        return;
    }

    if (focusedLink < 0)
    {
        focusedLink = (delta >= 0) ? 0 : (n - 1);
        return;
    }

    int next = focusedLink + delta;

    while (next < 0)
        next = next + n;

    while (next >= n)
        next = next - n;

    focusedLink = next;
}

void TerminalPage::focusFirstLinkFrom (int gridRow)
{
    const int n = (int) links.size();

    if (n == 0)
    {
        focusedLink = -1;
        return;
    }

    int i = 0;

    while (i < n)
    {
        if (links[i].y >= gridRow)
        {
            focusedLink = i;
            return;
        }

        i = i + 1;
    }

    focusedLink = 0;
}

const char* TerminalPage::getPath() { return currentPath.c_str(); }

int TerminalPage::open (const char* path, int columns)
{
    if (load (path) == 0)
        return 0;

    layout (columns);

    if (! doc)
        return 0;

    return 1;
}

int TerminalPage::followFocusedLink (int columns)
{
    error.clear();

    if (focusedLink < 0 || focusedLink >= (int) links.size())
        return 0;

    Link& link = links[focusedLink];

    std::string target;

    if (isRemoteDoc != 0)
    {
        // From a remote document every reference is a URL, including a
        // relative one, so it resolves against the document url rather than
        // against a directory on disk.
        Url resolved;

        if (resolveUrl (&documentUrl, link.href.c_str(), &resolved) == 0)
        {
            error.assign ("cannot follow ");
            error.append (link.href.c_str());
            return 0;
        }

        resolved.toString (&target);
    }
    else if (resolveHref (baseDir.c_str(), link.href.c_str(), &target) == 0)
    {
        error.assign ("cannot follow ");
        error.append (link.href.c_str());
        return 0;
    }

    // Remember where we were before the load replaces baseDir.
    std::string previous;
    previous.assign (currentPath.c_str());

    if (open (target.c_str(), columns) == 0)
        return 0;

    if (! previous.empty())
        history.push_back (previous);

    return 1;
}

int TerminalPage::canGoBack()
{
    return (history.size() > 0) ? 1 : 0;
}

int TerminalPage::goBack (int columns)
{
    error.clear();

    if (history.size() == 0)
        return 0;

    const int last = (int) history.size() - 1;

    std::string target;
    target.assign (history[last].c_str());
    history.pop_back();

    return open (target.c_str(), columns);
}

CellGrid* TerminalPage::getGrid() { return &grid; }
TerminalContainer* TerminalPage::getContainer() { return &container; }
const char* TerminalPage::getTitle() { return container.getCaption()->c_str(); }
const char* TerminalPage::getError() { return error.c_str(); }
int TerminalPage::getRows() { return rows; }

} // namespace headless
