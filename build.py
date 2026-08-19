#!/usr/bin/env python3
"""Build script for juce_litehtml.

Replaces make/cmake for the non-JUCE targets. Compiles each translation unit
into build/<target>/obj, tracks header dependencies via gcc's -MMD output, and
only recompiles what actually changed.

Usage:
    ./build.py                    build the default target (headless)
    ./build.py headless           build the headless test renderer
    ./build.py --release          optimised build
    ./build.py --clean            remove build artefacts for the target
    ./build.py --jobs 8           limit parallelism
    ./build.py --run doc.html     build, then run the binary on doc.html
    ./build.py --list             show known targets
    ./build.py cairo --crust      build with ../crust instead of gcc/g++

--crust needs github.com/brentharts/crust cloned beside this repository. It
lowers C++ to C with crust's cpprust.py and compiles the result with crust's
own backend; the lowering is cached under /tmp (override with $CRUST_TMP)
because it is slow and depends only on the source and its headers.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
MODULE = ROOT / "juce_litehtml"
LITEHTML = MODULE / "litehtml"
QUICKJS = MODULE / "quickjs"
GUMBO = LITEHTML / "src" / "gumbo"

CC = os.environ.get("CC", "gcc")
CXX = os.environ.get("CXX", "g++")


# ---------------------------------------------------------------------------
# Target description
# ---------------------------------------------------------------------------

@dataclass
class Target:
    name: str
    description: str
    binary: str
    sources: list = field(default_factory=list)          # list of Path
    include_dirs: list = field(default_factory=list)     # list of Path
    defines: list = field(default_factory=list)
    c_flags: list = field(default_factory=list)
    cxx_flags: list = field(default_factory=list)
    link_flags: list = field(default_factory=list)
    pkg_config: list = field(default_factory=list)


def glob_sources(*patterns_rooted):
    """Collect sources from (directory, pattern) pairs, sorted for determinism."""
    out = []
    for directory, pattern in patterns_rooted:
        out.extend(sorted(Path(directory).rglob(pattern)))
    return out


def litehtml_sources():
    srcs = glob_sources(
        (LITEHTML / "src", "*.cpp"),
        (LITEHTML / "src", "*.c"),
    )
    # gumbo ships a Ragel grammar and a Windows-only shim; neither is built.
    return [s for s in srcs if "visualc" not in s.parts]


def quickjs_sources():
    srcs = sorted(QUICKJS.glob("*.c"))
    # quickjs-bjson is an optional binary-JSON module nothing here uses.
    # quickjs-libc is compiled in: the renderer does not touch it, but it is
    # what gives mininodejs console.log and the std and os modules.
    skip = {"quickjs-bjson.c"}
    return [s for s in srcs if s.name not in skip]


ENGINE_INCLUDES = [
    LITEHTML / "include",
    LITEHTML / "include" / "litehtml",
    LITEHTML / "src",
    GUMBO / "include",
    GUMBO / "include" / "gumbo",
    QUICKJS,
]

ENGINE_DEFINES = [
    "LITEHTML_UTF8=1",
    "JS_STRICT_NAN_BOXING=1",
    "CONFIG_BIGNUM=1",
    "CONFIG_JSX=1",
    "_GNU_SOURCE",
]


def make_headless_target() -> Target:
    tls_defines, tls_pkgs = tls_config()
    return Target(
        name="headless",
        description="Headless renderer and test harness (no JUCE, no GTK)",
        binary="litehtml-headless",
        sources=(
            litehtml_sources()
            + quickjs_sources()
            + sorted((ROOT / "headless").glob("*.cpp"))
        ),
        include_dirs=ENGINE_INCLUDES + [ROOT / "headless"],
        defines=ENGINE_DEFINES + tls_defines,
        # GCC 13 turned -Wchanges-meaning into an error; litehtml's element.h
        # trips it. Suppressing beats patching vendored code.
        cxx_flags=["-std=c++17", "-Wno-changes-meaning"],
        c_flags=["-std=c11", "-Wno-unused-result"],
        link_flags=["-lm", "-lpthread", "-ldl", "-lz"],
        pkg_config=tls_pkgs,
    )


HEADLESS_CORE = ["container.cpp", "context.cpp", "el_script.cpp",
                 "imageinfo.cpp", "links.cpp", "loader.cpp", "strbuf.cpp",
                 "url.cpp"]


def tls_config():
    """TLS is optional: https works when openssl is present, and the build
    still succeeds without it (https then reports that it is unsupported)."""
    if shutil.which("pkg-config") and subprocess.run(
        ["pkg-config", "--exists", "openssl"]
    ).returncode == 0:
        return ["HEADLESS_TLS"], ["openssl"]
    return [], []


def make_cairo_target() -> Target:
    """Cairo front end: real fonts, renders to PNG, needs no window system."""
    cairo_dir = ROOT / "cairo"
    tls_defines, tls_pkgs = tls_config()
    return Target(
        name="cairo",
        description="Cairo renderer, outputs PNG (no window system needed)",
        binary="litehtml-cairo",
        sources=(
            litehtml_sources()
            + quickjs_sources()
            + [ROOT / "headless" / s for s in HEADLESS_CORE]
            + sorted(cairo_dir.glob("*.cpp"))
        ),
        include_dirs=ENGINE_INCLUDES + [ROOT / "headless", cairo_dir],
        defines=ENGINE_DEFINES + tls_defines,
        cxx_flags=["-std=c++17", "-Wno-changes-meaning"],
        c_flags=["-std=c11", "-Wno-unused-result"],
        link_flags=["-lm", "-lpthread", "-ldl", "-lz"],
        pkg_config=["cairo"] + tls_pkgs,
    )


def _terminal_target(name, description, binary, backend_srcs, pkgs, extra_link):
    term_dir = ROOT / "terminal"
    tls_defines, tls_pkgs = tls_config()
    return Target(
        name=name,
        description=description,
        binary=binary,
        sources=(
            litehtml_sources()
            + quickjs_sources()
            + [ROOT / "headless" / s for s in HEADLESS_CORE]
            + [term_dir / "cellgrid.cpp",
               term_dir / "terminal_container.cpp",
               term_dir / "terminal_page.cpp"]
            + [term_dir / s for s in backend_srcs]
        ),
        include_dirs=ENGINE_INCLUDES + [ROOT / "headless", term_dir],
        defines=ENGINE_DEFINES + tls_defines,
        cxx_flags=["-std=c++17", "-Wno-changes-meaning"],
        c_flags=["-std=c11", "-Wno-unused-result"],
        link_flags=["-lm", "-lpthread", "-ldl", "-lz"] + extra_link,
        pkg_config=pkgs + tls_pkgs,
    )


def make_tui_target() -> Target:
    return _terminal_target(
        "tui", "Terminal renderer via ANSI escape codes (no dependencies)",
        "litehtml-tui", ["ansi_main.cpp"], [], [])


def make_ncurses_target() -> Target:
    return _terminal_target(
        "ncurses", "Interactive terminal browser: links, history (ncursesw)",
        "litehtml-ncurses", ["curses_main.cpp"], ["ncursesw"], [])


def make_notcurses_target() -> Target:
    return _terminal_target(
        "notcurses", "Interactive terminal browser: links, history, images (notcurses)",
        "litehtml-notcurses", ["notcurses_main.cpp"], ["notcurses"], [])


def make_mininode_target() -> Target:
    """A node-like command line runner over the same quickjs the browser links.

    Deliberately does not pull in litehtml: this is for testing the JavaScript
    engine on its own, and a dependency on the renderer would defeat that."""
    return Target(
        name="mininodejs",
        description="Headless JavaScript runner over quickjs (no litehtml)",
        binary="mininodejs",
        sources=quickjs_sources() + sorted((ROOT / "mininode").glob("*.cpp")),
        include_dirs=[QUICKJS, ROOT / "headless"],
        defines=list(ENGINE_DEFINES),
        cxx_flags=["-std=c++17"],
        c_flags=["-std=c11", "-Wno-unused-result"],
        link_flags=["-lm", "-lpthread", "-ldl"],
    )


def make_gtk_target() -> Target:
    """GTK front end. Builds on the cairo container, which does the drawing;
    GTK supplies a window and a different cairo_t."""
    gtk_dir = ROOT / "gtk"
    tls_defines, tls_pkgs = tls_config()
    return Target(
        name="gtk",
        description="GTK3 browser: links, history, images",
        binary="litehtml-gtk",
        sources=(
            litehtml_sources()
            + quickjs_sources()
            + [ROOT / "headless" / s for s in HEADLESS_CORE]
            + [ROOT / "cairo" / "cairo_container.cpp"]
            + sorted(gtk_dir.glob("*.cpp"))
        ),
        include_dirs=ENGINE_INCLUDES + [gtk_dir, ROOT / "headless", ROOT / "cairo"],
        defines=ENGINE_DEFINES + tls_defines,
        cxx_flags=["-std=c++17", "-Wno-changes-meaning"],
        c_flags=["-std=c11", "-Wno-unused-result"],
        link_flags=["-lm", "-lpthread", "-ldl", "-lz"],
        pkg_config=["gtk+-3.0", "cairo", "pangocairo"] + tls_pkgs,
    )


TARGETS = {
    "headless": make_headless_target,
    "cairo": make_cairo_target,
    "tui": make_tui_target,
    "ncurses": make_ncurses_target,
    "notcurses": make_notcurses_target,
    "mininodejs": make_mininode_target,
    "gtk": make_gtk_target,
}

DEFAULT_TARGET = "headless"


# ---------------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------------

def pkg_config_flags(packages, mode):
    """Returns cflags or libs for the given pkg-config packages."""
    if not packages:
        return []
    if shutil.which("pkg-config") is None:
        raise SystemExit(
            "error: pkg-config not found, needed for: " + " ".join(packages)
        )
    try:
        out = subprocess.run(
            ["pkg-config", mode] + list(packages),
            check=True, capture_output=True, text=True,
        ).stdout
    except subprocess.CalledProcessError as e:
        raise SystemExit(
            f"error: pkg-config failed for {' '.join(packages)}\n"
            f"       try: sudo apt install libgtk-3-dev\n{e.stderr.strip()}"
        )
    return shlex.split(out)


def object_path(target: Target, src: Path, objdir: Path) -> Path:
    """Maps a source path to a unique object path, preserving directory shape."""
    try:
        rel = src.relative_to(ROOT)
    except ValueError:
        rel = Path(src.name)
    return objdir / rel.with_suffix(rel.suffix + ".o")


def needs_rebuild(obj: Path, src: Path, dep: Path, cmdline: str, cmd_cache: dict) -> bool:
    if not obj.exists():
        return True

    # The compile command itself is part of the input.
    if cmd_cache.get(str(obj)) != cmdline:
        return True

    obj_mtime = obj.stat().st_mtime

    if src.stat().st_mtime > obj_mtime:
        return True

    # Header dependencies recorded by -MMD.
    if dep.exists():
        try:
            text = dep.read_text()
        except OSError:
            return True
        text = text.replace("\\\n", " ")
        if ":" in text:
            for token in text.split(":", 1)[1].split():
                p = Path(token)
                if p.exists() and p.stat().st_mtime > obj_mtime:
                    return True
    else:
        return True

    return False


def build_compile_command(target: Target, src: Path, obj: Path, dep: Path,
                          extra_cflags, optimise: str, debug: bool):
    is_c = src.suffix == ".c"
    compiler = CC if is_c else CXX

    cmd = [compiler, "-c", str(src), "-o", str(obj), "-MMD", "-MF", str(dep)]
    cmd += [optimise]
    if debug:
        cmd += ["-g"]
    cmd += ["-fPIC"]
    cmd += (target.c_flags if is_c else target.cxx_flags)
    cmd += [f"-D{d}" for d in target.defines]
    cmd += [f"-I{d}" for d in target.include_dirs]
    cmd += extra_cflags
    return cmd


def run_build(target: Target, jobs: int, release: bool, debug: bool, verbose: bool) -> Path:
    outdir = BUILD / target.name
    objdir = outdir / "obj"
    objdir.mkdir(parents=True, exist_ok=True)

    if not target.sources:
        raise SystemExit(
            f"error: target '{target.name}' has no sources yet.\n"
            f"       {target.description}"
        )

    extra_cflags = pkg_config_flags(target.pkg_config, "--cflags")
    extra_libs = pkg_config_flags(target.pkg_config, "--libs")

    optimise = "-O2" if release else "-O0"

    cache_file = outdir / "commands.json"
    cmd_cache = {}
    if cache_file.exists():
        try:
            cmd_cache = json.loads(cache_file.read_text())
        except (OSError, json.JSONDecodeError):
            cmd_cache = {}

    jobs_to_run = []
    objects = []

    for src in target.sources:
        obj = object_path(target, src, objdir)
        dep = obj.with_suffix(".d")
        obj.parent.mkdir(parents=True, exist_ok=True)
        objects.append(obj)

        cmd = build_compile_command(target, src, obj, dep, extra_cflags, optimise, debug)
        cmdline = " ".join(cmd)

        if needs_rebuild(obj, src, dep, cmdline, cmd_cache):
            jobs_to_run.append((src, obj, cmd, cmdline))

    total = len(jobs_to_run)

    if total == 0:
        print(f"[{target.name}] up to date ({len(objects)} objects)")
    else:
        print(f"[{target.name}] compiling {total} of {len(objects)} files "
              f"with {jobs} job(s)")

    failures = []
    done = [0]
    start = time.time()

    def compile_one(item):
        src, obj, cmd, cmdline = item
        if verbose:
            print(" ".join(cmd))
        proc = subprocess.run(cmd, capture_output=True, text=True)
        return src, obj, cmdline, proc

    if jobs_to_run:
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for src, obj, cmdline, proc in pool.map(compile_one, jobs_to_run):
                done[0] += 1
                rel = src.relative_to(ROOT) if src.is_relative_to(ROOT) else src

                if proc.returncode != 0:
                    failures.append((rel, proc.stderr))
                    print(f"  [{done[0]}/{total}] FAILED {rel}")
                else:
                    cmd_cache[str(obj)] = cmdline
                    if not verbose:
                        print(f"  [{done[0]}/{total}] {rel}")
                    if proc.stderr.strip():
                        sys.stderr.write(proc.stderr)

    cache_file.write_text(json.dumps(cmd_cache, indent=0))

    if failures:
        print(f"\n{len(failures)} file(s) failed to compile:\n", file=sys.stderr)
        for rel, err in failures:
            print(f"--- {rel} ---", file=sys.stderr)
            sys.stderr.write(err)
        raise SystemExit(1)

    binary = outdir / target.binary

    # Relink if any object is newer than the binary.
    relink = (not binary.exists()) or any(
        o.stat().st_mtime > binary.stat().st_mtime for o in objects
    )

    if relink:
        print(f"[{target.name}] linking {binary.relative_to(ROOT)}")
        link_cmd = [CXX, "-o", str(binary)] + [str(o) for o in objects]
        link_cmd += target.link_flags + extra_libs
        if verbose:
            print(" ".join(link_cmd))
        proc = subprocess.run(link_cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            sys.stderr.write(proc.stderr)
            raise SystemExit("error: link failed")
        if proc.stderr.strip():
            sys.stderr.write(proc.stderr)

    elapsed = time.time() - start
    print(f"[{target.name}] done in {elapsed:.1f}s -> {binary.relative_to(ROOT)}")
    return binary


# ---------------------------------------------------------------------------
# Crust toolchain (--crust)
# ---------------------------------------------------------------------------
#
# Crust (github.com/brentharts/crust) is a C/C++/Rust frontend that lowers
# everything to plain C and compiles that with its own backend (ShivyCX).
# A C++ translation unit therefore takes two steps:
#
#     foo.cpp --[crust/tools/cpprust.py]--> foo.c --[crust]--> foo.o
#
# The first step is the slow one -- cpprust re-reads every header it can
# splice, and half a minute for one file is normal. It depends only on the
# source and those headers, so its output is cached under /tmp and reused
# until one of them actually changes. A .c source skips straight to step two.
#
# None of this touches the gcc path: lowered C, objects and the binary all
# live in crust-specific directories, so both toolchains can coexist and
# `--clean` for one does not disturb the other.

CRUST_DIR = ROOT.parent / "crust"
CRUST_TMP = Path(os.environ.get("CRUST_TMP", "/tmp")) / "juce_litehtml-crust"

CRUST_CLONE_HINT = (
    "you need to git clone https://github.com/brentharts/crust.git "
    "side by side with juce_litehtml"
)


def find_crust() -> Path:
    """Return ../crust, or say how to get it.

    A directory that exists but has no `crust` driver in it is a partial or
    interrupted clone, which fails later and much less clearly, so it is
    reported the same way as no directory at all.
    """
    if not CRUST_DIR.is_dir() or not (CRUST_DIR / "crust").is_file():
        raise RuntimeError(CRUST_CLONE_HINT)
    return CRUST_DIR


def crust_env() -> dict:
    """Environment for invoking crust from any working directory.

    The `crust` driver does `import shivyc.main`, so the repository root has
    to be importable; without this it only runs when cwd happens to be that
    root.
    """
    env = os.environ.copy()
    existing = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = (
        f"{CRUST_DIR}{os.pathsep}{existing}" if existing else str(CRUST_DIR)
    )
    return env


# ShivyCX ships a small set of fallback headers (stdio, stdlib, string,
# stdint, ...) and does not search /usr/include, because glibc's headers are
# full of GNU extensions its frontend does not accept. Anything outside that
# set is simply missing, and the first `#include <assert.h>` stops the build.
#
# These shims fill the most common gaps. They are deliberately minimal --
# enough to get a translation unit through the frontend, not a libc. The
# shim directory is passed LAST on the include path, so a real header found
# anywhere else always wins.
_CRUST_SHIMS = {
    "assert.h": """/* crust build shim */
#ifndef _CRUST_SHIM_ASSERT_H
#define _CRUST_SHIM_ASSERT_H
extern void abort(void);
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
/* No stringify: the check survives, the message does not. */
#define assert(e) ((e) ? (void)0 : abort())
#endif
#endif
""",
    "limits.h": """/* crust build shim */
#ifndef _CRUST_SHIM_LIMITS_H
#define _CRUST_SHIM_LIMITS_H
#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define SHRT_MIN (-32768)
#define SHRT_MAX 32767
#define USHRT_MAX 65535
#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U
#define LONG_MIN (-9223372036854775807L - 1)
#define LONG_MAX 9223372036854775807L
#define ULONG_MAX 18446744073709551615UL
#define LLONG_MIN (-9223372036854775807LL - 1)
#define LLONG_MAX 9223372036854775807LL
#define ULLONG_MAX 18446744073709551615ULL
#define PATH_MAX 4096
#endif
""",
    "float.h": """/* crust build shim */
#ifndef _CRUST_SHIM_FLOAT_H
#define _CRUST_SHIM_FLOAT_H
#define FLT_RADIX 2
#define FLT_DIG 6
#define FLT_EPSILON 1.19209290e-7F
#define FLT_MIN 1.17549435e-38F
#define FLT_MAX 3.40282347e+38F
#define DBL_DIG 15
#define DBL_EPSILON 2.2204460492503131e-16
#define DBL_MIN 2.2250738585072014e-308
#define DBL_MAX 1.7976931348623157e+308
#endif
""",
    "errno.h": """/* crust build shim */
#ifndef _CRUST_SHIM_ERRNO_H
#define _CRUST_SHIM_ERRNO_H
extern int *__errno_location(void);
#define errno (*__errno_location())
#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define ERANGE 34
#endif
""",
    "inttypes.h": """/* crust build shim */
#ifndef _CRUST_SHIM_INTTYPES_H
#define _CRUST_SHIM_INTTYPES_H
#include <stdint.h>
#define PRId32 "d"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRId64 "ld"
#define PRIu64 "lu"
#define PRIx64 "lx"
#endif
""",
}

# cpprust leaves angle includes alone, so the C++ spellings survive into the
# lowered C. They are the same headers under different names.
_CRUST_CXX_SPELLINGS = {
    "cstdint": "stdint.h", "cstring": "string.h", "cstdlib": "stdlib.h",
    "cstdio": "stdio.h", "cstddef": "stddef.h", "cctype": "ctype.h",
    "cmath": "math.h", "cassert": "assert.h", "climits": "limits.h",
    "cfloat": "float.h", "cerrno": "errno.h", "cstdarg": "stdarg.h",
}


def crust_shim_dir() -> Path:
    """Materialise the shim headers under /tmp and return the directory."""
    d = CRUST_TMP / "shim"
    d.mkdir(parents=True, exist_ok=True)
    wanted = dict(_CRUST_SHIMS)
    for spelling, real in _CRUST_CXX_SPELLINGS.items():
        wanted[spelling] = f"/* crust build shim */\n#include <{real}>\n"
    for name, body in wanted.items():
        p = d / name
        # Rewrite only on change, so shim mtimes stay stable and do not
        # trigger a rebuild of everything on every invocation.
        if not p.exists() or p.read_text() != body:
            p.write_text(body)
    return d


def _file_digest(paths, extra=()) -> str:
    import hashlib
    h = hashlib.sha256()
    for p in sorted(str(x) for x in paths):
        try:
            h.update(Path(p).read_bytes())
        except OSError:
            h.update(b"<missing>")
        h.update(p.encode("utf-8", "replace"))
    for e in extra:
        h.update(str(e).encode("utf-8", "replace"))
    return h.hexdigest()


def _headers_under(dirs) -> list:
    out = []
    for d in dirs:
        d = Path(d)
        if not d.is_dir():
            continue
        for p in d.rglob("*"):
            if p.suffix in (".h", ".hpp", ".inc", ".hxx"):
                out.append(p)
    return out


def crust_artifact(src: Path, root: Path, suffix: str) -> Path:
    """Map a source to an artifact path, preserving directory shape.

    The shape matters: litehtml and quickjs both have a `cutils.c`-shaped
    name collision waiting to happen, and flattening to basenames would let
    one object silently overwrite another.
    """
    try:
        rel = src.relative_to(ROOT)
    except ValueError:
        rel = Path(src.name)
    return root / rel.with_suffix(rel.suffix + suffix)


def _crust_diagnostic(proc) -> str:
    """The useful part of a crust or cpprust failure.

    crust prints its diagnostics to stdout and then dies of an uncaught
    RuntimeError, so the traceback is all that reaches stderr. Reading only
    stderr -- the habit gcc teaches -- throws away the error and keeps the
    noise. Both streams are read here, the driver's `print(S)` banner and the
    traceback are dropped, and ANSI colour is stripped so the grouped summary
    can compare messages as text.
    """
    import re
    text = ((proc.stdout or "") + "\n" + (proc.stderr or ""))
    text = re.sub(r"\x1b\[[0-9;]*m", "", text)
    keep, skipping = [], False
    for line in text.splitlines():
        if line.startswith("<module 'shivyc.main'"):
            continue
        if line.startswith("Traceback (most recent call last)"):
            skipping = True
            continue
        if skipping:
            # The traceback runs to the final `RuntimeError: N`, which only
            # repeats the exit status we already have.
            if line and not line[0].isspace():
                skipping = False
                if line.startswith("RuntimeError:"):
                    continue
            else:
                continue
        keep.append(line)
    out = "\n".join(keep).strip()
    return out or f"exited with status {proc.returncode}"


def crust_lower_command(crust: Path, src: Path, out_c: Path,
                        include_dirs, defines) -> list:
    cmd = [sys.executable, str(crust / "tools" / "cpprust.py"),
           str(src), "-o", str(out_c)]
    for d in include_dirs:
        cmd += ["--incdir", str(d)]
    for name in defines:
        cmd += ["-D", name]
    return cmd


def crust_compile_command(crust: Path, src: Path, obj: Path,
                          include_dirs, defines, release: bool) -> list:
    cmd = [sys.executable, str(crust / "crust"), "-c", str(src), "-o", str(obj)]
    cmd += ["-O", "2" if release else "0"]
    for d in include_dirs:
        cmd += ["-I", str(d)]
    for name in defines:
        cmd += ["-D", name]
    return cmd


def run_crust_build(target: Target, jobs: int, release: bool, verbose: bool,
                    timeout: int) -> Path:
    """Build a target with crust instead of gcc/g++."""
    crust = find_crust()

    tmp = CRUST_TMP / target.name
    lowered_dir = tmp / "lowered"
    objdir = tmp / "obj"
    objdir.mkdir(parents=True, exist_ok=True)
    lowered_dir.mkdir(parents=True, exist_ok=True)

    outdir = BUILD / f"{target.name}-crust"
    outdir.mkdir(parents=True, exist_ok=True)

    if not target.sources:
        raise SystemExit(
            f"error: target '{target.name}' has no sources yet.\n"
            f"       {target.description}"
        )

    extra_cflags = pkg_config_flags(target.pkg_config, "--cflags")
    extra_libs = pkg_config_flags(target.pkg_config, "--libs")

    # pkg-config hands back -I and -D mixed with flags crust has no opinion
    # on; only those two are meaningful here.
    pkg_includes = [f[2:] for f in extra_cflags if f.startswith("-I")]
    pkg_defines = [f[2:] for f in extra_cflags if f.startswith("-D")]

    include_dirs = (
        [str(d) for d in target.include_dirs]
        + pkg_includes
        + [str(crust_shim_dir())]          # last: never shadows a real header
    )
    defines = list(target.defines) + pkg_defines

    print(f"[{target.name}] crust: {crust}")
    print(f"[{target.name}] cache: {tmp}")

    # Every header that could be spliced into a lowering, hashed once. This
    # is the expensive-but-correct part of the cache key: editing any header
    # in the include path invalidates the lowerings that could have read it.
    header_digest = _file_digest(
        _headers_under(target.include_dirs),
        extra=["v1"],
    )
    translator_digest = _file_digest([
        crust / "tools" / "cpprust.py",
        crust / "tools" / "cpp_auto.py",
    ])

    manifest_path = tmp / "manifest.json"
    manifest = {}
    if manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text())
        except (OSError, json.JSONDecodeError):
            manifest = {}

    work = []
    objects = []

    for src in target.sources:
        is_cpp = src.suffix != ".c"
        obj = crust_artifact(src, objdir, ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        objects.append(obj)

        lowered = crust_artifact(src, lowered_dir, ".c") if is_cpp else None
        if lowered is not None:
            lowered.parent.mkdir(parents=True, exist_ok=True)

        lower_key = _file_digest(
            [src],
            extra=[header_digest, translator_digest, "|".join(defines)],
        ) if is_cpp else ""

        compile_input = lowered if is_cpp else src
        cmd = crust_compile_command(crust, compile_input, obj,
                                    include_dirs, defines, release)
        obj_key = _file_digest([src], extra=[lower_key, " ".join(cmd)])

        entry = manifest.get(str(src), {})
        need_lower = is_cpp and (
            entry.get("lower_key") != lower_key or not lowered.exists()
        )
        need_compile = (
            need_lower
            or entry.get("obj_key") != obj_key
            or not obj.exists()
        )

        if need_compile:
            work.append({
                "src": src, "obj": obj, "lowered": lowered,
                "is_cpp": is_cpp, "need_lower": need_lower,
                "lower_key": lower_key, "obj_key": obj_key, "cmd": cmd,
                "lower_cmd": crust_lower_command(
                    crust, src, lowered, include_dirs, defines
                ) if is_cpp else None,
            })

    total = len(work)
    if total == 0:
        print(f"[{target.name}] up to date ({len(objects)} objects)")
    else:
        n_lower = sum(1 for w in work if w["need_lower"])
        print(f"[{target.name}] {total} of {len(objects)} files to build "
              f"({n_lower} need lowering) with {jobs} job(s)")

    start = time.time()
    done = [0]
    failures = []
    env = crust_env()

    def build_one(w):
        # Stage 1: C++ -> C, only when the cache missed.
        if w["need_lower"]:
            if verbose:
                print(" ".join(w["lower_cmd"]))
            try:
                p = subprocess.run(w["lower_cmd"], capture_output=True,
                                   text=True, timeout=timeout, env=env)
            except subprocess.TimeoutExpired:
                return w, "lower", f"timed out after {timeout}s"
            if p.returncode != 0:
                # cpprust writes its diagnostic into the -o file on refusal,
                # so the file existing is not success; the status is.
                return w, "lower", _crust_diagnostic(p)

        # Stage 2: C -> object.
        if verbose:
            print(" ".join(w["cmd"]))
        try:
            p = subprocess.run(w["cmd"], capture_output=True, text=True,
                               timeout=timeout, env=env)
        except subprocess.TimeoutExpired:
            return w, "compile", f"timed out after {timeout}s"
        if p.returncode != 0:
            return w, "compile", _crust_diagnostic(p)
        return w, None, ""

    if work:
        # `as_completed`, not `map`: `map` yields in submission order, so one
        # slow file at the front held back every result behind it and a
        # half-hour run printed nothing at all. Lowering times vary by more
        # than an order of magnitude, which makes that the normal case.
        from concurrent.futures import as_completed
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [pool.submit(build_one, w) for w in work]
            for fut in as_completed(futures):
                w, stage, msg = fut.result()
                done[0] += 1
                src = w["src"]
                rel = src.relative_to(ROOT) if src.is_relative_to(ROOT) else src
                if stage is None:
                    manifest[str(src)] = {
                        "lower_key": w["lower_key"],
                        "obj_key": w["obj_key"],
                    }
                    print(f"  [{done[0]}/{total}] {rel}", flush=True)
                else:
                    # Drop any stale entry so a fixed source is retried.
                    manifest.pop(str(src), None)
                    failures.append((rel, stage, msg))
                    print(f"  [{done[0]}/{total}] FAILED ({stage}) {rel}",
                          flush=True)

    manifest_path.write_text(json.dumps(manifest, indent=0))

    if failures:
        print(f"\n{len(failures)} file(s) failed under crust:\n", file=sys.stderr)
        for rel, stage, msg in failures:
            print(f"--- {rel} [{stage}] ---", file=sys.stderr)
            print(msg + "\n", file=sys.stderr)
        # Group by message shape: one refusal in a shared header fails every
        # file that includes it, and the count is what says which to fix.
        shapes = {}
        for _, stage, msg in failures:
            first = " ".join(msg.split())[:120]
            shapes[(stage, first)] = shapes.get((stage, first), 0) + 1
        print("failures by cause:", file=sys.stderr)
        for (stage, first), n in sorted(shapes.items(), key=lambda kv: -kv[1]):
            print(f"  {n:3d}  [{stage}] {first}", file=sys.stderr)
        raise SystemExit(1)

    binary = outdir / target.binary
    relink = (not binary.exists()) or any(
        o.stat().st_mtime > binary.stat().st_mtime for o in objects
    )
    if relink:
        print(f"[{target.name}] linking {binary.relative_to(ROOT)}")
        link_cmd = [sys.executable, str(crust / "crust"), "-o", str(binary)]
        link_cmd += [str(o) for o in objects]
        link_cmd += target.link_flags + extra_libs
        if verbose:
            print(" ".join(link_cmd))
        p = subprocess.run(link_cmd, capture_output=True, text=True, env=env)
        if p.returncode != 0:
            sys.stderr.write(_crust_diagnostic(p) + "\n")
            raise SystemExit("error: crust link failed")

    elapsed = time.time() - start
    print(f"[{target.name}] done in {elapsed:.1f}s -> {binary.relative_to(ROOT)}")
    return binary


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Build juce_litehtml targets without make or cmake.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("target", nargs="?", default=DEFAULT_TARGET,
                    help=f"target to build (default: {DEFAULT_TARGET})")
    ap.add_argument("-j", "--jobs", type=int, default=0,
                    help="parallel compile jobs (default: number of CPUs)")
    ap.add_argument("--release", action="store_true", help="optimised build (-O2)")
    ap.add_argument("--debug", action="store_true", help="include debug symbols (-g)")
    ap.add_argument("--crust", action="store_true",
                    help="compile with ../crust instead of gcc/g++")
    ap.add_argument("--crust-timeout", type=int, default=900, metavar="SEC",
                    help="per-file timeout under --crust (default: 900)")
    ap.add_argument("--clean", action="store_true", help="delete this target's artefacts")
    ap.add_argument("--list", action="store_true", help="list available targets")
    ap.add_argument("-v", "--verbose", action="store_true", help="echo compile commands")
    ap.add_argument("--run", nargs=argparse.REMAINDER,
                    help="after building, run the binary with these arguments")
    args = ap.parse_args()

    if args.list:
        for name, factory in TARGETS.items():
            t = factory()
            state = "ready" if t.sources else "no sources yet"
            marker = " (default)" if name == DEFAULT_TARGET else ""
            print(f"  {name}{marker}: {t.description} [{state}]")
        return

    if args.target not in TARGETS:
        raise SystemExit(
            f"error: unknown target '{args.target}'. "
            f"Known targets: {', '.join(TARGETS)}"
        )

    if args.clean:
        # --crust owns different directories, including the /tmp cache, so it
        # cleans those and leaves the gcc build alone (and vice versa).
        removed = False
        outdirs = [BUILD / f"{args.target}-crust", CRUST_TMP / args.target] \
            if args.crust else [BUILD / args.target]
        for d in outdirs:
            if d.exists():
                shutil.rmtree(d)
                rel = d.relative_to(ROOT) if d.is_relative_to(ROOT) else d
                print(f"removed {rel}")
                removed = True
        if not removed:
            print("nothing to clean")
        return

    jobs = args.jobs if args.jobs > 0 else (os.cpu_count() or 1)
    target = TARGETS[args.target]()

    if args.crust:
        binary = run_crust_build(target, jobs, args.release, args.verbose,
                                 args.crust_timeout)
    else:
        binary = run_build(target, jobs, args.release, args.debug, args.verbose)

    if args.run is not None:
        print()
        sys.exit(subprocess.run([str(binary)] + args.run).returncode)


if __name__ == "__main__":
    main()
