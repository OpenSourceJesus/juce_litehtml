#pragma once

#include "crust_compat.h"

namespace headless {

enum UrlScheme {
    UrlSchemeFile = 0,
    UrlSchemeHttp = 1,
    UrlSchemeHttps = 2
};

/** A parsed URL.

    Everything up to now has been a filesystem path with a base directory
    glued on the front. That stops working the moment a document can come off
    the network, because "../style.css" has to mean something different
    depending on where the document itself came from. So paths and URLs are
    now one type, with a local file being the scheme that happens to have no
    host.
 */
class Url
{
public:
    int scheme;
    std::string host;
    int port;               // 0 means the scheme default
    std::string path;       // includes any query string
    std::string fragment;

    Url();

    int isRemote();

    /** Port to actually connect to, filling in the scheme default. */
    int effectivePort();

    /** Serialises back to text. */
    void toString (std::string* out);

    /** The directory part of the path, for resolving a relative reference. */
    void directory (std::string* out);
};

/** Parses an absolute URL. Returns 1 when a scheme was present and
    understood, 0 otherwise -- a bare path is not an absolute URL.
 */
int parseUrl (const char* text, Url* out);

/** Resolves a reference against a base, the way a browser would.

    Handles an absolute URL, a scheme-relative "//host/path", a root-relative
    "/path", and a relative "a/b.html" or "../b.html", collapsing "." and ".."
    as it goes. Returns 1 on success.
 */
int resolveUrl (Url* base, const char* reference, Url* out);

/** Builds a Url for a local filesystem path. */
void urlFromPath (const char* path, Url* out);

} // namespace headless
