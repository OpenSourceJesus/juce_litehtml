#!/usr/bin/env python3
"""Network tests for the loader.

These are separate from run_tests.py because they need a server. The script
starts its own on a loopback port, so nothing here depends on the outside
world -- except the optional TLS check, which is skipped by default.

Usage:
    ./test_network.py            run the local-server tests
    ./test_network.py --tls URL  also fetch one real https URL
"""

from __future__ import annotations

import argparse
import gzip
import http.server
import socketserver
import subprocess
import sys
import threading
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / "build" / "headless" / "litehtml-headless"
PORT = 8791

PAGES = {
    "/index.html": (200, "text/html",
                    b"<html><head><title>Index</title>"
                    b'<link rel="stylesheet" href="style.css"></head><body>'
                    b"<h1>Index</h1><p class='lead'>styled</p>"
                    b'<p><img src="logo.png"> pic</p>'
                    b'<p><a href="sub/deep.html">deep</a></p></body></html>'),
    "/style.css": (200, "text/css",
                   b"body{font-family:monospace}h1{font-size:32px;font-weight:bold}"
                   b".lead{font-style:italic}"),
    "/sub/deep.html": (200, "text/html",
                       b"<html><head><title>Deep</title>"
                       b'<link rel="stylesheet" href="../style.css"></head><body>'
                       b"<h1>Deep</h1><p class='lead'>parent css</p>"
                       b'<p><img src="../logo.png"> pic</p></body></html>'),
}

# A tiny valid PNG, 64x32, so image sizing has something real to read.
PNG = None


def make_png():
    global PNG
    import struct
    import zlib

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    w, h = 64, 32
    raw = b"".join(b"\x00" + bytes([200, 60, 60] * w) for _ in range(h))
    PNG = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw))
           + chunk(b"IEND", b""))


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        p = self.path
        if p == "/logo.png":
            self.send_response(200)
            self.send_header("Content-Type", "image/png")
            self.send_header("Content-Length", str(len(PNG)))
            self.end_headers()
            self.wfile.write(PNG)
        elif p == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/index.html")
            self.end_headers()
        elif p == "/loop":
            self.send_response(302)
            self.send_header("Location", "/loop")
            self.end_headers()
        elif p == "/gzipped.html":
            # Compressed even though the request asked for identity, which is
            # what a CDN in front of a large site generally does.
            body = (b"<html><head><title>Gzipped</title>"
                    b'<link rel="stylesheet" href="/gzipped.css"></head><body>'
                    b"<p class='v'>compressed</p></body></html>")
            data = gzip.compress(body)
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif p == "/gzipped.css":
            data = gzip.compress(b":root{--c:#3366cc}.v{color:var(--c)}")
            self.send_response(200)
            self.send_header("Content-Type", "text/css")
            self.send_header("Content-Encoding", "gzip")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        elif p == "/chunked.html":
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for part in (b"<html><head><title>Chunked</title></head><body>",
                         b"<p>first</p>", b"<p>second</p>", b"</body></html>"):
                self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif p == "/rate.html":
            # First hit 429 with Retry-After: 1; then serve. Exercises retry
            # and ensures a transient miss is not cached as permanent failure.
            n = getattr(self.server, "rate_hits", 0)
            self.server.rate_hits = n + 1
            if n == 0:
                self.send_response(429)
                self.send_header("Retry-After", "1")
                self.send_header("Content-Length", "0")
                self.end_headers()
            else:
                body = b"<html><head><title>Rate</title></head>" \
                       b"<body><p>after-retry</p></body></html>"
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        elif p == "/rate-img.html":
            body = (b"<html><body><img src='/rate.png' width='64' height='32'>"
                    b"</body></html>")
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif p == "/rate.png":
            n = getattr(self.server, "rate_png_hits", 0)
            self.server.rate_png_hits = n + 1
            if n == 0:
                self.send_response(429)
                self.send_header("Retry-After", "1")
                self.end_headers()
            else:
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(PNG)))
                self.end_headers()
                self.wfile.write(PNG)
        elif p in PAGES:
            code, ctype, body = PAGES[p]
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"nope")


def run(args, expect_ok=True):
    proc = subprocess.run([str(BINARY)] + args, capture_output=True, text=True,
                          timeout=40)
    return proc.returncode, proc.stdout, proc.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tls", metavar="URL",
                    help="also fetch one real https URL (needs the internet)")
    args = ap.parse_args()

    if not BINARY.exists():
        print("error: build the headless target first", file=sys.stderr)
        return 1

    make_png()
    socketserver.TCPServer.allow_reuse_address = True
    srv = socketserver.TCPServer(("127.0.0.1", PORT), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    base = f"http://127.0.0.1:{PORT}"
    checks = []

    def check(name, cond, detail=""):
        checks.append((name, cond, detail))

    # Network is off unless asked for.
    rc, out, err = run([f"{base}/index.html", "-m", "text"])
    check("refuses network by default", rc != 0 and "disabled" in err, err.strip())

    rc, out, err = run(["--net", f"{base}/index.html", "-m", "text"])
    check("fetches over http", rc == 0 and "Index" in out, err.strip())

    # The stylesheet is a separate request; monospace proves it was applied.
    rc, out, err = run(["--net", f"{base}/index.html", "-m", "draw"])
    check("applies remote stylesheet", "monospace/32/bold" in out, out[:80])
    check("sizes remote image", "64x32" in out, out[:200])

    # ../style.css and ../logo.png from a subdirectory.
    rc, out, err = run(["--net", f"{base}/sub/deep.html", "-m", "draw"])
    check("resolves parent-relative css", "monospace/32/bold" in out, out[:80])
    check("resolves parent-relative image", "64x32" in out, out[:200])

    rc, out, err = run(["--net", f"{base}/redirect", "-m", "text"])
    check("follows redirect", rc == 0 and "Index" in out, err.strip())

    rc, out, err = run(["--net", f"{base}/loop", "-m", "text"])
    check("stops a redirect loop", rc != 0 and "redirect" in err, err.strip())

    rc, out, err = run(["--net", f"{base}/chunked.html", "-m", "text"])
    check("decodes chunked encoding",
          "first" in out and "second" in out, out.strip()[:80])

    rc, out, err = run(["--net", f"{base}/missing.html", "-m", "text"])
    check("reports 404", rc != 0 and "404" in err, err.strip())

    # A compressed page, and a compressed stylesheet inside it. Without
    # decompression both are binary noise: the page renders empty and every
    # variable the stylesheet defines silently goes undefined.
    rc, out, err = run(["--net", f"{base}/gzipped.html", "-m", "text"])
    check("decompresses gzip", rc == 0 and "compressed" in out,
          (out + err).strip()[:80])

    rc, out, err = run(["--net", f"{base}/gzipped.html", "-m", "draw"])
    check("uses variables from a gzipped stylesheet", "#3366cc" in out,
          out.strip()[:120])

    rc, out, err = run(["--net", f"{base}/rate.html", "-m", "text"])
    check("retries after HTTP 429",
          rc == 0 and "after-retry" in out, (out + err).strip()[:120])

    rc, out, err = run(["--net", f"{base}/rate-img.html", "-m", "draw"])
    check("retries image after 429 (not cached as fail)",
          "64x32" in out, (out + err).strip()[:200])

    if args.tls:
        rc, out, err = run(["--net", args.tls, "-m", "stats"])
        check("fetches over https", rc == 0 and "size:" in out, err.strip())

    srv.shutdown()

    failed = 0
    for name, ok, detail in checks:
        print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
        if not ok:
            failed += 1
            if detail:
                print(f"        {detail}")

    print()
    print(f"{len(checks) - failed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
