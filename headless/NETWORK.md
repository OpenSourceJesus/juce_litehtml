# Network loading

`ResourceLoader` (headless/loader.h) fetches by URL: local files, http, and
https where TLS is compiled in. The container uses it for stylesheets,
scripts and images, so a document fetched over http pulls its own resources
the same way it was pulled.

## Network is off by default

```cpp
container.getLoader()->setNetworkEnabled (1);
```

Every binary takes `--net`; without it, an http URL is refused with a message
naming the URL. A loader that reached the network unasked would make the test
suite depend on the outside world and would fetch things a caller may not
expect, so it has to be turned on deliberately.

```sh
./build/headless/litehtml-headless --net -m text https://example.com/
./build/ncurses/litehtml-ncurses --net http://example.com/
```

## URLs, not paths

Everything used to be a filesystem path with a base directory glued on the
front. That stops working the moment a document can come off the network,
because `../style.css` has to mean something different depending on where the
document itself came from. So `headless/url.h` has one type for both, a local
file being the scheme with no host, and `resolveUrl` handles absolute,
scheme-relative (`//host/path`), root-relative (`/path`) and relative
references, collapsing `.` and `..`.

`setBaseDirectory` still works and is now shorthand for a `file` document url
ending in a slash, so the two spellings cannot drift apart.

After a redirect the document url is the URL that was *actually fetched*, not
the one asked for. Otherwise every relative link on the landing page would
resolve against the wrong directory.

## What the loader handles

- Redirects, up to a limit (default 5), including relative `Location` headers.
  A redirect loop is reported rather than followed.
- Chunked transfer-encoding, which servers use even for `Connection: close`.
- A response size cap (default 16 MB), because a loader with no limit is a
  denial of service waiting for a large file.
- Connect and read timeouts (default 15s).
- TLS with certificate verification, SNI, and hostname checking. Without
  `SSL_set1_host` verification would pass for any valid certificate from
  anyone, which is not verification.

## Caching

Everything is cached by absolute URL, because a page pulls the same resource
more than once: `get_image_size` asks for a size, then something asks for the
pixels. Without the cache, laying out a page would fetch each image twice.

Failures are cached too, so a broken image is not retried on every relayout --
except a refusal caused by the network being disabled, which is deliberately
*not* cached, since enabling the network later should not be defeated by a
refusal recorded before it.

**The cache is binary-safe, and that took a fix.** It originally stored and
returned bodies through `c_str()`, which truncates at the first NUL -- a PNG
reaches one inside its first chunk header. The bug only appeared on the
*second* use of a resource, since the first came straight from the socket, so
it showed up as an image that rendered on load and vanished on revisit.

## TLS is optional

`build.py` probes for openssl and, when it is there, compiles with
`-DHEADLESS_TLS` and links it. Without it everything still builds and https
reports that it is unsupported rather than failing to link. On Ubuntu:
`sudo apt install libssl-dev`.

## Testing

```sh
./test_network.py                     # local server, no internet needed
./test_network.py --tls https://...   # also check one real https fetch
```

The suite starts its own loopback server, so it is self-contained. The golden
tests in `run_tests.py` stay entirely offline.

## Known gaps

- No `Content-Type` handling: a document is assumed to be HTML and the charset
  is assumed to be UTF-8. A page served as latin-1 will render wrongly.
- No compression: `Accept-Encoding: identity` is sent, so a server that
  ignores it and gzips anyway will produce garbage.
- No cookies, no authentication, no POST.
- No connection reuse -- one connection per resource, `Connection: close`.
  A page with many images pays a handshake each time.
- The cache never expires and ignores `Cache-Control`.
- Fetches are synchronous and serial, so a slow resource blocks layout.
