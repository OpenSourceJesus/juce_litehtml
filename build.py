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
    # quickjs-bjson is an optional module the engine does not need here, and
    # quickjs-libc pulls in a POSIX shell/IO layer we deliberately leave out
    # of the headless renderer.
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
        link_flags=["-lm", "-lpthread", "-ldl"],
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
        link_flags=["-lm", "-lpthread", "-ldl"],
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
        link_flags=["-lm", "-lpthread", "-ldl"] + extra_link,
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


def make_gtk_target() -> Target:
    """GTK front end. Sources do not exist yet -- this is the next step."""
    gtk_dir = ROOT / "gtk"
    return Target(
        name="gtk",
        description="GTK3 front end (not implemented yet)",
        binary="litehtml-gtk",
        sources=(
            litehtml_sources()
            + quickjs_sources()
            + sorted(gtk_dir.glob("*.cpp"))
        ),
        include_dirs=ENGINE_INCLUDES + [gtk_dir, ROOT / "headless"],
        defines=list(ENGINE_DEFINES),
        cxx_flags=["-std=c++17", "-Wno-changes-meaning"],
        c_flags=["-std=c11", "-Wno-unused-result"],
        link_flags=["-lm", "-lpthread", "-ldl"],
        pkg_config=["gtk+-3.0", "cairo", "pango", "pangocairo"],
    )


TARGETS = {
    "headless": make_headless_target,
    "cairo": make_cairo_target,
    "tui": make_tui_target,
    "ncurses": make_ncurses_target,
    "notcurses": make_notcurses_target,
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
        outdir = BUILD / args.target
        if outdir.exists():
            shutil.rmtree(outdir)
            print(f"removed {outdir.relative_to(ROOT)}")
        else:
            print("nothing to clean")
        return

    jobs = args.jobs if args.jobs > 0 else (os.cpu_count() or 1)
    target = TARGETS[args.target]()

    binary = run_build(target, jobs, args.release, args.debug, args.verbose)

    if args.run is not None:
        print()
        sys.exit(subprocess.run([str(binary)] + args.run).returncode)


if __name__ == "__main__":
    main()
