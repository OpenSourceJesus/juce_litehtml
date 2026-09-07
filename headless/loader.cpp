#include "loader.h"
#include "strbuf.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#ifdef HEADLESS_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace headless {

CacheEntry::CacheEntry()
{
    ok = 0;
}

//==============================================================================

ResourceLoader::ResourceLoader()
{
    networkEnabled = 0;
    timeoutSeconds = 15;
    maxRedirects = 5;
    maxBytes = 16 * 1024 * 1024;
    transientFailure = 0;
}

ResourceLoader::~ResourceLoader()
{
}

void ResourceLoader::setNetworkEnabled (int enabled) { networkEnabled = enabled; }
int ResourceLoader::isNetworkEnabled() { return networkEnabled; }
void ResourceLoader::setTimeoutSeconds (int seconds) { timeoutSeconds = seconds; }
void ResourceLoader::setMaxRedirects (int count) { maxRedirects = count; }
void ResourceLoader::setMaxBytes (int bytes) { maxBytes = bytes; }
const char* ResourceLoader::getError() { return error.c_str(); }
void ResourceLoader::clearCache() { cache.clear(); }

int ResourceLoader::hasTls()
{
#ifdef HEADLESS_TLS
    return 1;
#else
    return 0;
#endif
}

//==============================================================================

int ResourceLoader::findCached (const char* key, std::string* body)
{
    int i = 0;

    while (i < (int) cache.size())
    {
        CacheEntry& entry = cache[i];

        if (strcmp (entry.url.c_str(), key) == 0)
        {
            // Assign the string, not its c_str(): a cached image is binary
            // and c_str() would truncate it at the first NUL -- which a PNG
            // reaches inside its first chunk header. The bug only showed on
            // the *second* use of a resource, since the first came straight
            // from the socket.
            if (entry.ok != 0 && body != 0)
                *body = entry.body;

            return (entry.ok != 0) ? 1 : -1;
        }

        i = i + 1;
    }

    return 0;
}

void ResourceLoader::storeCached (const char* key, std::string* body, int ok)
{
    CacheEntry entry;
    entry.url.assign (key);
    entry.ok = ok;

    if (ok != 0 && body != 0)
        entry.body = *body;

    cache.push_back (entry);
}

//==============================================================================

//==============================================================================

/** Decode %XX sequences in a file path. HTTP request paths must stay encoded;
    fopen needs the on-disk bytes (Ye_Olde_Boar's.png, not ...%27s.png).
 */
static void percentDecodePath (std::string* path)
{
    if (path == 0 || path->empty())
        return;

    std::string out;
    const char* p = path->c_str();
    int i = 0;

    while (p[i] != '\0')
    {
        if (p[i] == '%' && p[i + 1] != '\0' && p[i + 2] != '\0')
        {
            char hex[3];
            hex[0] = p[i + 1];
            hex[1] = p[i + 2];
            hex[2] = '\0';

            char* end = 0;
            const long v = strtol (hex, &end, 16);

            if (end == hex + 2 && v >= 0 && v <= 255)
            {
                out.push_back ((char) v);
                i = i + 3;
                continue;
            }
        }

        out.push_back (p[i]);
        i = i + 1;
    }

    *path = out;
}

int ResourceLoader::fetchFile (Url* url, std::string* body)
{
    std::string path = url->path;
    percentDecodePath (&path);

    FILE* f = fopen (path.c_str(), "rb");

    if (f == 0)
    {
        error.assign ("cannot open ");
        error.append (path.c_str());
        return 0;
    }

    char buffer[8192];
    size_t n = fread (buffer, 1, 8192, f);
    int total = 0;

    while (n > 0)
    {
        total = total + (int) n;

        if (total > maxBytes)
        {
            fclose (f);
            error.assign ("file too large");
            return 0;
        }

        // A file may hold nulls (an image does), so append by length rather
        // than treating the buffer as a C string.
        int k = 0;

        while (k < (int) n)
        {
            body->push_back (buffer[k]);
            k = k + 1;
        }

        n = fread (buffer, 1, 8192, f);
    }

    fclose (f);
    return 1;
}

//==============================================================================
// HTTP
//==============================================================================

/** Reads a header value out of a response header block, case-insensitively.
    Returns 1 when found.
 */
static int headerValue (const char* headers, const char* name, std::string* out)
{
    const int nameLen = (int) strlen (name);
    int i = 0;

    while (headers[i] != '\0')
    {
        // At the start of a line?
        const int atLineStart = (i == 0 || headers[i - 1] == '\n') ? 1 : 0;

        if (atLineStart != 0 && strncasecmp (headers + i, name, nameLen) == 0)
        {
            int j = i + nameLen;

            while (headers[j] == ' ')
                j = j + 1;

            if (headers[j] == ':')
            {
                j = j + 1;

                while (headers[j] == ' ' || headers[j] == '\t')
                    j = j + 1;

                out->clear();

                while (headers[j] != '\0' && headers[j] != '\r' && headers[j] != '\n')
                {
                    out->push_back (headers[j]);
                    j = j + 1;
                }

                return 1;
            }
        }

        i = i + 1;
    }

    return 0;
}

/** Inflates a gzip or zlib stream in place. Returns 1 on success.

    Servers compress whether or not identity was requested -- a CDN in front
    of a large site generally always will -- so this has to exist even though
    the request asks for uncompressed data. Without it the body is binary
    noise, which for HTML means an empty page and for CSS means every
    variable in it silently goes undefined.
 */
static int inflateBody (std::string* body, int rawDeflate)
{
    if (body == 0 || body->empty())
        return 0;

    z_stream zs;
    memset (&zs, 0, sizeof (zs));

    // 47 = auto-detect gzip or zlib headers. A server claiming "deflate"
    // sometimes means raw deflate with no header at all, hence the retry.
    const int windowBits = (rawDeflate != 0) ? -15 : 47;

    if (inflateInit2 (&zs, windowBits) != Z_OK)
        return 0;

    zs.next_in = (Bytef*) body->c_str();
    zs.avail_in = (uInt) body->size();

    std::string out;
    char buffer[32768];
    int status = Z_OK;

    while (status != Z_STREAM_END)
    {
        zs.next_out = (Bytef*) buffer;
        zs.avail_out = 32768;

        status = inflate (&zs, Z_NO_FLUSH);

        if (status != Z_OK && status != Z_STREAM_END)
        {
            inflateEnd (&zs);
            return 0;
        }

        const int produced = 32768 - (int) zs.avail_out;
        int k = 0;

        while (k < produced)
        {
            out.push_back (buffer[k]);
            k = k + 1;
        }

        if (produced == 0 && status != Z_STREAM_END)
            break;
    }

    inflateEnd (&zs);

    if (out.empty())
        return 0;

    *body = out;
    return 1;
}

/** Decodes a chunked transfer-encoded body in place. */
static int decodeChunked (std::string* body)
{
    std::string out;
    const char* p = body->c_str();
    const int len = (int) body->size();
    int i = 0;

    while (i < len)
    {
        // Chunk size in hex, up to the CRLF.
        int size = 0;
        int digits = 0;

        while (i < len)
        {
            const char c = p[i];
            int v = -1;

            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;

            if (v < 0)
                break;

            size = size * 16 + v;
            digits = digits + 1;
            i = i + 1;
        }

        if (digits == 0)
            return 0;

        // Skip any chunk extension, then the CRLF.
        while (i < len && p[i] != '\n')
            i = i + 1;

        i = i + 1;

        if (size == 0)
            break;

        int k = 0;

        while (k < size && i < len)
        {
            out.push_back (p[i]);
            i = i + 1;
            k = k + 1;
        }

        // Trailing CRLF after the chunk data.
        while (i < len && p[i] != '\n')
            i = i + 1;

        i = i + 1;
    }

    *body = out;
    return 1;
}

/** A connection, plain or TLS, so the request/response code is written once. */
class Connection
{
public:
    Connection();

    int fd;

#ifdef HEADLESS_TLS
    SSL_CTX* ctx;
    SSL* ssl;
#endif

    int write (const char* data, int length);
    int read (char* buffer, int capacity);
    void close();
};

Connection::Connection()
{
    fd = -1;

#ifdef HEADLESS_TLS
    ctx = 0;
    ssl = 0;
#endif
}

int Connection::write (const char* data, int length)
{
#ifdef HEADLESS_TLS
    if (ssl != 0)
        return SSL_write (ssl, data, length);
#endif

    return (int) send (fd, data, (size_t) length, 0);
}

int Connection::read (char* buffer, int capacity)
{
#ifdef HEADLESS_TLS
    if (ssl != 0)
        return SSL_read (ssl, buffer, capacity);
#endif

    return (int) recv (fd, buffer, (size_t) capacity, 0);
}

void Connection::close()
{
#ifdef HEADLESS_TLS
    if (ssl != 0)
    {
        SSL_shutdown (ssl);
        SSL_free (ssl);
        ssl = 0;
    }

    if (ctx != 0)
    {
        SSL_CTX_free (ctx);
        ctx = 0;
    }
#endif

    if (fd >= 0)
    {
        ::close (fd);
        fd = -1;
    }
}

int ResourceLoader::fetchHttp (Url* url, std::string* body, Url* finalUrl, int depth)
{
    if (depth > maxRedirects)
    {
        error.assign ("too many redirects");
        return 0;
    }

    if (url->scheme == UrlSchemeHttps && hasTls() == 0)
    {
        error.assign ("https is not supported in this build (no TLS)");
        return 0;
    }

    // Resolve the host.
    struct addrinfo hints;
    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char portText[16];
    snprintf (portText, sizeof (portText), "%d", url->effectivePort());

    struct addrinfo* results = 0;

    if (getaddrinfo (url->host.c_str(), portText, &hints, &results) != 0 || results == 0)
    {
        error.assign ("cannot resolve ");
        error.append (url->host.c_str());
        return 0;
    }

    Connection conn;

    struct addrinfo* ai = results;

    while (ai != 0)
    {
        const int s = socket (ai->ai_family, ai->ai_socktype, ai->ai_protocol);

        if (s >= 0)
        {
            struct timeval tv;
            tv.tv_sec = timeoutSeconds;
            tv.tv_usec = 0;
            setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));
            setsockopt (s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof (tv));

            if (connect (s, ai->ai_addr, ai->ai_addrlen) == 0)
            {
                conn.fd = s;
                ai = 0;
                continue;
            }

            ::close (s);
        }

        ai = ai->ai_next;
    }

    freeaddrinfo (results);

    if (conn.fd < 0)
    {
        error.assign ("cannot connect to ");
        error.append (url->host.c_str());
        return 0;
    }

#ifdef HEADLESS_TLS
    if (url->scheme == UrlSchemeHttps)
    {
        conn.ctx = SSL_CTX_new (TLS_client_method());

        if (conn.ctx == 0)
        {
            conn.close();
            error.assign ("cannot create TLS context");
            return 0;
        }

        SSL_CTX_set_default_verify_paths (conn.ctx);
        SSL_CTX_set_verify (conn.ctx, SSL_VERIFY_PEER, 0);

        conn.ssl = SSL_new (conn.ctx);

        if (conn.ssl == 0)
        {
            conn.close();
            error.assign ("cannot create TLS session");
            return 0;
        }

        SSL_set_fd (conn.ssl, conn.fd);

        // SNI, and hostname verification. Without the first, a shared host
        // serves the wrong certificate; without the second, verification
        // passes for any valid certificate from anyone.
        SSL_set_tlsext_host_name (conn.ssl, url->host.c_str());
        SSL_set1_host (conn.ssl, url->host.c_str());

        if (SSL_connect (conn.ssl) != 1)
        {
            const long verify = SSL_get_verify_result (conn.ssl);
            conn.close();

            error.assign ("TLS handshake failed for ");
            error.append (url->host.c_str());

            if (verify != 0)
            {
                error.append (": ");
                error.append (X509_verify_cert_error_string (verify));
            }

            return 0;
        }
    }
#endif

    // Send the request. Connection: close keeps the response framing simple:
    // the body ends when the socket does, unless the server chunks it.
    std::string request;
    request.assign ("GET ");
    request.append (url->path.empty() ? "/" : url->path.c_str());
    request.append (" HTTP/1.1\r\nHost: ");
    request.append (url->host.c_str());

    if (url->port > 0)
    {
        const int def = (url->scheme == UrlSchemeHttps) ? 443 : 80;

        if (url->port != def)
        {
            request.push_back (':');
            strAppendInt (&request, url->port);
        }
    }

    // Wikimedia (and others) rate-limit or block generic/spoofed Mozilla
    // User-Agents. Identify the client and point at the project page.
    // See: https://foundation.wikimedia.org/wiki/Policy:Wikimedia_Foundation_User-Agent_Policy
    request.append ("\r\nUser-Agent: juce_litehtml-headless/1.0 "
                    "(+https://github.com/crustos/juce_litehtml)\r\n");
    request.append ("Accept: */*\r\n");
    request.append ("Accept-Encoding: gzip, deflate, identity\r\n");
    request.append ("Connection: close\r\n\r\n");

    int sent = 0;

    while (sent < (int) request.size())
    {
        const int n = conn.write (request.c_str() + sent, (int) request.size() - sent);

        if (n <= 0)
        {
            conn.close();
            error.assign ("failed to send request");
            return 0;
        }

        sent = sent + n;
    }

    // Read the whole response.
    std::string response;
    char buffer[8192];
    int n = conn.read (buffer, 8192);

    while (n > 0)
    {
        if ((int) response.size() + n > maxBytes)
        {
            conn.close();
            error.assign ("response too large");
            return 0;
        }

        int k = 0;

        while (k < n)
        {
            response.push_back (buffer[k]);
            k = k + 1;
        }

        n = conn.read (buffer, 8192);
    }

    conn.close();

    if (response.empty())
    {
        error.assign ("empty response");
        return 0;
    }

    // Split headers from body at the blank line.
    const char* raw = response.c_str();
    const int total = (int) response.size();
    int split = -1;
    int i = 0;

    while (i + 3 < total)
    {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n')
        {
            split = i;
            break;
        }

        i = i + 1;
    }

    if (split < 0)
    {
        error.assign ("malformed response");
        return 0;
    }

    std::string headers;
    int k = 0;

    while (k < split)
    {
        headers.push_back (raw[k]);
        k = k + 1;
    }

    std::string content;
    k = split + 4;

    while (k < total)
    {
        content.push_back (raw[k]);
        k = k + 1;
    }

    // Status line: "HTTP/1.1 200 OK".
    int status = 0;
    const char* h = headers.c_str();
    int sp = 0;

    while (h[sp] != '\0' && h[sp] != ' ')
        sp = sp + 1;

    if (h[sp] == ' ')
        status = atoi (h + sp + 1);

    std::string encoding;

    if (headerValue (h, "Transfer-Encoding", &encoding) != 0)
    {
        if (strcasecmp (encoding.c_str(), "chunked") == 0)
        {
            if (decodeChunked (&content) == 0)
            {
                error.assign ("malformed chunked response");
                return 0;
            }
        }
    }

    std::string contentEncoding;

    if (headerValue (h, "Content-Encoding", &contentEncoding) != 0)
    {
        if (strcasecmp (contentEncoding.c_str(), "gzip") == 0)
        {
            if (inflateBody (&content, 0) == 0)
            {
                error.assign ("could not decompress a gzip response");
                return 0;
            }
        }
        else if (strcasecmp (contentEncoding.c_str(), "deflate") == 0)
        {
            // Try with a header first, then as a raw stream.
            std::string copy = content;

            if (inflateBody (&content, 0) == 0)
            {
                content = copy;

                if (inflateBody (&content, 1) == 0)
                {
                    error.assign ("could not decompress a deflate response");
                    return 0;
                }
            }
        }
        else if (strcasecmp (contentEncoding.c_str(), "identity") != 0)
        {
            error.assign ("unsupported Content-Encoding: ");
            error.append (contentEncoding.c_str());
            return 0;
        }
    }

    if (status >= 300 && status <= 399)
    {
        std::string location;

        if (headerValue (h, "Location", &location) == 0)
        {
            error.assign ("redirect with no Location");
            return 0;
        }

        Url next;

        if (resolveUrl (url, location.c_str(), &next) == 0)
        {
            error.assign ("cannot resolve redirect target");
            return 0;
        }

        return fetchHttp (&next, body, finalUrl, depth + 1);
    }

    if (status == 429 || status == 503)
    {
        // Rate limit / temporary unavailable: honour Retry-After (capped),
        // retry within the same redirect budget, and mark transient so the
        // miss is not cached as permanent.
        if (depth >= maxRedirects)
        {
            transientFailure = 1;
            error.assign ("http ");
            strAppendInt (&error, status);
            error.append (" for ");

            std::string text;
            url->toString (&text);
            error.append (text.c_str());

            return 0;
        }

        int waitSec = 1;
        std::string retryAfter;

        if (headerValue (h, "Retry-After", &retryAfter) != 0)
        {
            // Numeric seconds only; HTTP-date form is rare and ignored.
            const int parsed = atoi (retryAfter.c_str());

            if (parsed > 0)
                waitSec = parsed;
        }

        if (waitSec > 5)
            waitSec = 5;

        sleep ((unsigned) waitSec);
        return fetchHttp (url, body, finalUrl, depth + 1);
    }

    if (status < 200 || status > 299)
    {
        error.assign ("http ");
        strAppendInt (&error, status);
        error.append (" for ");

        std::string text;
        url->toString (&text);
        error.append (text.c_str());

        return 0;
    }

    *body = content;

    if (finalUrl != 0)
        *finalUrl = *url;

    return 1;
}

//==============================================================================

int ResourceLoader::fetch (Url* url, std::string* body, Url* finalUrl)
{
    if (url == 0 || body == 0)
        return 0;

    error.clear();
    body->clear();
    transientFailure = 0;

    if (finalUrl != 0)
        *finalUrl = *url;

    std::string key;
    url->toString (&key);

    const int cached = findCached (key.c_str(), body);

    if (cached == 1)
        return 1;

    if (cached == -1)
    {
        error.assign ("previously failed: ");
        error.append (key.c_str());
        return 0;
    }

    int ok = 0;

    if (url->isRemote() != 0)
    {
        if (networkEnabled == 0)
        {
            error.assign ("network access is disabled (");
            error.append (key.c_str());
            error.append (")");

            // Deliberately not cached as a failure: enabling the network
            // later should not be defeated by a refusal recorded before it.
            return 0;
        }

        ok = fetchHttp (url, body, finalUrl, 0);
    }
    else
    {
        ok = fetchFile (url, body);
    }

    // ponytail: 429/503 are temporary — caching them as permanent misses
    // blanked Wikipedia Main Page thumbs after the first rate-limit hit.
    if (ok != 0 || transientFailure == 0)
        storeCached (key.c_str(), body, ok);

    if (ok == 0)
        body->clear();

    return ok;
}

} // namespace headless
