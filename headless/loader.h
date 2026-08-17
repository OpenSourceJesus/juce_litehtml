#pragma once

#include "crust_compat.h"
#include "url.h"

namespace headless {

/** One cached resource. */
class CacheEntry
{
public:
    std::string url;
    std::string body;
    int ok;

    CacheEntry();
};

/** Fetches resources by URL: local files, http, and https where TLS is
    compiled in.

    A document pulls the same stylesheet or image more than once in practice
    -- get_image_size asks for a size and then something asks for the pixels
    -- so everything is cached by absolute URL. Without that, laying out a
    page would fetch each image twice.

    Network access is off by default. A loader that reaches the network
    without being asked to would make the test suite depend on the outside
    world and would fetch things a caller may not expect, so it has to be
    turned on explicitly.
 */
class ResourceLoader
{
public:
    ResourceLoader();
    ~ResourceLoader();

    /** Allows http and https. Off by default. */
    void setNetworkEnabled (int enabled);
    int isNetworkEnabled();

    /** Socket timeout in seconds, for connect and for each read. */
    void setTimeoutSeconds (int seconds);

    /** How many redirects to follow before giving up. */
    void setMaxRedirects (int count);

    /** Largest response accepted, in bytes. A loader with no limit is a
        denial of service waiting for a large file.
     */
    void setMaxBytes (int bytes);

    /** Fetches a URL. Returns 1 on success and fills body. On a redirect
        chain, finalUrl ends up at whatever was actually fetched, which is
        what relative references in the result must resolve against.
     */
    int fetch (Url* url, std::string* body, Url* finalUrl);

    /** Reports why the last fetch failed. */
    const char* getError();

    void clearCache();

    /** Returns 1 when TLS was compiled in. */
    static int hasTls();

private:
    int fetchFile (Url* url, std::string* body);
    int fetchHttp (Url* url, std::string* body, Url* finalUrl, int depth);
    int findCached (const char* key, std::string* body);
    void storeCached (const char* key, std::string* body, int ok);

    int networkEnabled;
    int timeoutSeconds;
    int maxRedirects;
    int maxBytes;
    std::string error;
    std::ownvector<CacheEntry> cache;
};

} // namespace headless
