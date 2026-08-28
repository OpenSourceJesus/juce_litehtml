#!/usr/bin/env python3
"""Checks the ahead-of-time WebAssembly path.

A `.wasm` in `wasm/modules/` is translated to C by Crust at build time and
linked in, so by the time these run there is no compiler involved and nothing
is generated on the fly. The checks confirm the module is present, that its
exports compute the right answers through JavaScript, and that the parts of
the standard API this build cannot honour say so rather than misbehaving.

Skipped, not failed, when the build has no bundled modules: wasm is optional
here in the same way cairo and TLS are, and a build without Crust beside the
repository must still be testable.

    ./test_wasm.py
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MININODE = ROOT / "build" / "mininodejs" / "mininodejs"
HEADLESS = ROOT / "build" / "headless" / "litehtml-headless"

failures = []
passes = 0


def check(name, got, want):
    global passes
    if got == want:
        passes += 1
        print("  ok    %s" % name)
    else:
        failures.append(name)
        print("  FAIL  %s\n          got:  %r\n          want: %r"
              % (name, got, want))


def run_js(code):
    p = subprocess.run([str(MININODE), "-e", code],
                       capture_output=True, text=True, timeout=60)
    return (p.stdout + p.stderr).strip()


def main():
    if not MININODE.is_file():
        print("build/mininodejs/mininodejs missing -- run ./build.py "
              "mininodejs")
        return 2

    if "undefined" in run_js('console.log(typeof WebAssembly)'):
        print("no bundled wasm modules in this build (crust absent?) "
              "-- skipping")
        return 0

    check("module is bundled",
          run_js('console.log(WebAssembly.bundled.indexOf("demo") >= 0)'),
          "true")

    check("exports are present",
          run_js('const e = WebAssembly.instantiateBundled("demo")'
                 '.instance.exports;'
                 'console.log(["add","fib","sum_to","gcd"]'
                 '.every(n => typeof e[n] === "function"))'),
          "true")

    # The answers matter more than the plumbing: these go through the
    # translated C, so a wrong one means the round trip changed the program.
    check("add",
          run_js('console.log(WebAssembly.instantiateBundled("demo")'
                 '.instance.exports.add(20, 22))'), "42")
    check("fib",
          run_js('console.log(WebAssembly.instantiateBundled("demo")'
                 '.instance.exports.fib(20))'), "6765")
    check("sum_to",
          run_js('console.log(WebAssembly.instantiateBundled("demo")'
                 '.instance.exports.sum_to(100))'), "5050")
    check("gcd",
          run_js('console.log(WebAssembly.instantiateBundled("demo")'
                 '.instance.exports.gcd(48, 18))'), "6")

    # Recursion through the translated module, called repeatedly: a module is
    # initialised once and stays usable.
    check("repeated instantiation",
          run_js('let s = 0;'
                 'for (let i = 0; i < 3; i++)'
                 '  s += WebAssembly.instantiateBundled("demo")'
                 '.instance.exports.fib(10);'
                 'console.log(s)'), "165")

    check("unknown module reports itself",
          run_js('try { WebAssembly.instantiateBundled("nope"); }'
                 'catch (e) { console.log(e.message.indexOf("nope") >= 0); }'),
          "true")

    # The unsupported half of the API must fail at the call with an
    # explanation, not be missing.
    check("instantiate is present and explains itself",
          run_js('console.log(typeof WebAssembly.instantiate === "function")'),
          "true")
    check("instantiate rejects",
          run_js('try { WebAssembly.instantiate(new Uint8Array()); }'
                 'catch (e) { console.log(e.message'
                 '.indexOf("compiled into it") >= 0); }'), "true")

    # And the same thing from a page, which is the point of the exercise.
    if HEADLESS.is_file():
        page = ROOT / "build" / "wasm-test.html"
        page.write_text(
            "<html><body><script>\n"
            "var i = WebAssembly.instantiateBundled('demo').instance;\n"
            "throw new Error('R=' + i.exports.fib(15));\n"
            "</script></body></html>\n")
        p = subprocess.run([str(HEADLESS), "--js", "-m", "text", str(page)],
                           capture_output=True, text=True, timeout=60)
        check("runs from a <script> in a page",
              "R=610" in (p.stdout + p.stderr), True)

        # Without --js nothing runs, which is what keeps the golden suite
        # byte-identical.
        p = subprocess.run([str(HEADLESS), "-m", "text", str(page)],
                           capture_output=True, text=True, timeout=60)
        check("scripts stay off by default",
              "R=610" in (p.stdout + p.stderr), False)

    print("\n%d passed, %d failed" % (passes, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
