#pragma once

#include "../headless/context.h"
#include "../headless/crust_compat.h"
#include "../headless/links.h"
#include "terminal_container.h"

namespace headless {

/** An image placed in the laid-out document, in grid cells, with its src
    already resolved to a local path.
 */
class PageImage
{
public:
    std::string path;
    int x;
    int y;
    int width;
    int height;

    PageImage();
};

/** Everything the three terminal backends share: read a file, lay it out at
    a column count, and paint the grid. Kept out of the backends so ANSI,
    ncurses and notcurses differ only in how they put the grid on screen.
 */
class TerminalPage
{
public:
    TerminalPage();
    ~TerminalPage();

    /** Reads from a path, a URL, or stdin when path is 0 or "-". Returns 1
        on success; on failure the reason is in getError().
     */
    int load (const char* path);

    /** Allows http and https. Off by default, as in the loader. */
    void setNetworkEnabled (int enabled);

    /** Lays out at the given column count and repaints the grid. */
    void layout (int columns);

    CellGrid* getGrid();
    TerminalContainer* getContainer();

    /** Document title, or an empty string. */
    const char* getTitle();

    const char* getError();

    /** Rows the laid-out document occupies. */
    int getRows();

    //== Links =================================================================

    /** Links in the current document, in document order, with boxes already
        converted to grid cells.
     */
    std::ownvector<Link>* getLinks();

    /** Images in the current document. A backend that can draw pixels blits
        these over the placeholder the grid already holds.
     */
    std::ownvector<PageImage>* getImages();

    /** Index of the focused link, or -1. */
    int getFocusedLink();
    void setFocusedLink (int index);

    /** Moves focus by delta, wrapping. Does nothing with no links. */
    void moveFocus (int delta);

    /** Focuses the first link at or after a grid row, so tabbing follows what
        is on screen rather than jumping back to the top.
     */
    void focusFirstLinkFrom (int gridRow);

    /** Follows the focused link. Returns 1 when a new document was loaded, 0
        when there is nothing to follow (external URL, fragment, missing file);
        getError() says which.
     */
    int followFocusedLink (int columns);

    /** Returns 1 when there is somewhere to go back to. */
    int canGoBack();

    /** Goes back one entry. Returns 1 on success. */
    int goBack (int columns);

    /** Path of the current document, or an empty string for stdin. */
    const char* getPath();

private:
    /** Loads a path and lays it out, without touching history. */
    int open (const char* path, int columns);

    void refreshLinks();
    void refreshImages();
    Context context;
    TerminalContainer container;
    CellGrid grid;
    litehtml::document::ptr doc;
    std::string html;
    std::string baseDir;
    Url documentUrl;
    int isRemoteDoc;
    std::string error;
    std::string currentPath;
    int rows;

    std::ownvector<Link> links;
    std::ownvector<PageImage> images;
    int focusedLink;

    // Visited paths, most recent last. The current page is not on it.
    std::ownvector<std::string> history;
};

} // namespace headless
