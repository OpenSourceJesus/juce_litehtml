#include "url.h"
#include "strbuf.h"

#include <stdlib.h>
#include <string.h>

namespace headless {

Url::Url()
{
    scheme = UrlSchemeFile;
    port = 0;
}

int Url::isRemote()
{
    return (scheme == UrlSchemeHttp || scheme == UrlSchemeHttps) ? 1 : 0;
}

int Url::effectivePort()
{
    if (port > 0)
        return port;

    if (scheme == UrlSchemeHttps)
        return 443;

    if (scheme == UrlSchemeHttp)
        return 80;

    return 0;
}

void Url::toString (std::string* out)
{
    if (out == 0)
        return;

    out->clear();

    if (scheme == UrlSchemeFile)
    {
        out->append (path.c_str());
        return;
    }

    if (scheme == UrlSchemeHttps)
        out->append ("https://");
    else
        out->append ("http://");

    out->append (host.c_str());

    if (port > 0)
    {
        // A non-default port has to survive the round trip, or a redirect
        // back to the same server would land on 80.
        const int def = (scheme == UrlSchemeHttps) ? 443 : 80;

        if (port != def)
        {
            out->push_back (':');
            strAppendInt (out, port);
        }
    }

    if (path.empty() || path[0] != '/')
        out->push_back ('/');

    out->append (path.c_str());
}

void Url::directory (std::string* out)
{
    if (out == 0)
        return;

    out->clear();

    const char* p = path.c_str();
    int lastSlash = -1;
    int i = 0;

    while (p[i] != '\0')
    {
        if (p[i] == '/')
            lastSlash = i;

        i = i + 1;
    }

    if (lastSlash < 0)
        return;

    int k = 0;

    while (k <= lastSlash)
    {
        out->push_back (p[k]);
        k = k + 1;
    }
}

//==============================================================================

void urlFromPath (const char* path, Url* out)
{
    if (out == 0)
        return;

    out->scheme = UrlSchemeFile;
    out->host.clear();
    out->port = 0;
    out->fragment.clear();
    out->path.clear();

    if (path != 0)
        out->path.assign (path);
}

/** Splits a trailing "#fragment" off, returning the length before it. */
static int splitFragment (const char* text, std::string* fragment)
{
    int i = 0;

    while (text[i] != '\0' && text[i] != '#')
        i = i + 1;

    if (text[i] == '#' && fragment != 0)
        fragment->assign (text + i + 1);

    return i;
}

int parseUrl (const char* text, Url* out)
{
    if (text == 0 || out == 0)
        return 0;

    int scheme = -1;
    int offset = 0;

    if (strncmp (text, "http://", 7) == 0)       { scheme = UrlSchemeHttp;  offset = 7; }
    else if (strncmp (text, "https://", 8) == 0) { scheme = UrlSchemeHttps; offset = 8; }
    else if (strncmp (text, "file://", 7) == 0)  { scheme = UrlSchemeFile;  offset = 7; }
    else return 0;

    out->scheme = scheme;
    out->host.clear();
    out->port = 0;
    out->path.clear();
    out->fragment.clear();

    const char* rest = text + offset;
    const int end = splitFragment (rest, &out->fragment);

    if (scheme == UrlSchemeFile)
    {
        int i = 0;

        while (i < end)
        {
            out->path.push_back (rest[i]);
            i = i + 1;
        }

        return 1;
    }

    // Authority runs to the first '/', and may carry a ":port".
    int i = 0;

    while (i < end && rest[i] != '/')
    {
        out->host.push_back (rest[i]);
        i = i + 1;
    }

    // Split the port back off the host.
    int colon = -1;
    int k = 0;

    while (k < (int) out->host.size())
    {
        if (out->host[k] == ':')
            colon = k;

        k = k + 1;
    }

    if (colon >= 0)
    {
        std::string hostOnly;
        std::string portText;
        int j = 0;

        while (j < colon)
        {
            hostOnly.push_back (out->host[j]);
            j = j + 1;
        }

        j = colon + 1;

        while (j < (int) out->host.size())
        {
            portText.push_back (out->host[j]);
            j = j + 1;
        }

        out->host = hostOnly;
        out->port = atoi (portText.c_str());
    }

    while (i < end)
    {
        out->path.push_back (rest[i]);
        i = i + 1;
    }

    if (out->path.empty())
        out->path.assign ("/");

    return 1;
}

//==============================================================================

/** Collapses "." and ".." segments, in place. */
static void normalisePath (std::string* path)
{
    if (path == 0 || path->empty())
        return;

    const int rooted = ((*path)[0] == '/') ? 1 : 0;

    // Split into segments, dropping "." and popping on "..".
    std::ownvector<std::string> segments;
    std::string current;

    const char* p = path->c_str();
    int i = 0;

    while (1)
    {
        const char c = p[i];

        if (c == '/' || c == '\0')
        {
            if (current.empty())
            {
                // Repeated slash; nothing to add.
            }
            else if (strcmp (current.c_str(), ".") == 0)
            {
                // Current directory; nothing to add.
            }
            else if (strcmp (current.c_str(), "..") == 0)
            {
                if (segments.size() > 0)
                {
                    const int last = (int) segments.size() - 1;

                    if (strcmp (segments[last].c_str(), "..") != 0)
                        segments.pop_back();
                    else
                        segments.push_back (current);
                }
                else if (rooted == 0)
                {
                    // A relative path may legitimately start above its base.
                    segments.push_back (current);
                }
            }
            else
            {
                segments.push_back (current);
            }

            current.clear();

            if (c == '\0')
                break;
        }
        else
        {
            current.push_back (c);
        }

        i = i + 1;
    }

    // A trailing slash in the input means a directory, and is preserved.
    const int trailing = (path->size() > 0 && (*path)[path->size() - 1] == '/') ? 1 : 0;

    std::string out;

    if (rooted != 0)
        out.push_back ('/');

    int k = 0;

    while (k < (int) segments.size())
    {
        if (k > 0)
            out.push_back ('/');

        out.append (segments[k].c_str());
        k = k + 1;
    }

    if (trailing != 0 && ! out.empty() && out[out.size() - 1] != '/')
        out.push_back ('/');

    if (out.empty())
        out.assign (rooted != 0 ? "/" : ".");

    *path = out;
}

int resolveUrl (Url* base, const char* reference, Url* out)
{
    if (reference == 0 || out == 0)
        return 0;

    // An absolute reference replaces the base entirely.
    if (parseUrl (reference, out) != 0)
    {
        normalisePath (&out->path);
        return 1;
    }

    if (base == 0)
    {
        urlFromPath (reference, out);
        return 1;
    }

    out->scheme = base->scheme;
    out->host.assign (base->host.c_str());
    out->port = base->port;
    out->path.clear();
    out->fragment.clear();

    std::string ref;
    const int end = splitFragment (reference, &out->fragment);
    int i = 0;

    while (i < end)
    {
        ref.push_back (reference[i]);
        i = i + 1;
    }

    // A bare "#fragment" keeps the base document.
    if (ref.empty())
    {
        out->path.assign (base->path.c_str());
        return 1;
    }

    const char* r = ref.c_str();

    // Scheme-relative: "//host/path" keeps the scheme and replaces the rest.
    if (r[0] == '/' && r[1] == '/')
    {
        std::string synth;

        if (base->scheme == UrlSchemeHttps)
            synth.assign ("https:");
        else if (base->scheme == UrlSchemeHttp)
            synth.assign ("http:");
        else
            synth.assign ("file:");

        synth.append (r);

        if (parseUrl (synth.c_str(), out) != 0)
        {
            normalisePath (&out->path);
            return 1;
        }

        return 0;
    }

    if (r[0] == '/')
    {
        out->path.assign (r);
        normalisePath (&out->path);
        return 1;
    }

    // Relative: join onto the base's directory.
    std::string dir;
    base->directory (&dir);

    out->path.assign (dir.c_str());
    out->path.append (r);

    normalisePath (&out->path);

    return 1;
}

} // namespace headless
