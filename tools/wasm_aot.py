#!/usr/bin/env python3
"""Translate bundled `.wasm` modules to C, ahead of time.

    python3 tools/wasm_aot.py wasm/modules build/wasm-aot

For each `.wasm` in the input directory this writes two files: the module
translated to C by Crust's `tools/wasm2c.py`, and a small piece of glue that
registers its exports with `headless/wasm_registry.cpp`. `build.py` runs this
before compiling and adds the results to the source list, so a `.wasm` dropped
into `wasm/modules/` is linked into the next build with nothing else to edit.

The translation happens **here, at build time**, and not in the browser. A
module is ordinary compiled code by the time a page can reach it: no compiler
is needed at runtime, nothing is written to disk while browsing, and a page
cannot supply a module of its own. That last point is the limitation and the
safety property at once -- a translated module is native code with the
process's full privileges, so only modules shipped with the build are run.

Crust is expected beside this repository, which is where `build.py --crust`
already looks for it.
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Value type -> the one-character kind the registry uses. Matches the
# characters documented in headless/wasm_registry.h.
KIND = {0x7F: "i", 0x7E: "j", 0x7D: "f", 0x7C: "d"}


def find_crust() -> Path | None:
    """Crust tree that has wasm2c (and preferably --target wasm).

    Prefers $CRUST_DIR, then ../crust, then ../crust-brentharts. The last
    is for a local brentharts/crust checkout kept beside an older fork that
    does not yet carry the wasm tooling.
    """
    candidates = []
    env = os.environ.get("CRUST_DIR")
    if env:
        candidates.append(Path(env))
    candidates.append(ROOT.parent / "crust")
    candidates.append(ROOT.parent / "crust-brentharts")
    for c in candidates:
        if (c / "tools" / "wasm2c.py").is_file():
            return c
    return None


def crust_available() -> bool:
    return find_crust() is not None


def crust_dir() -> Path:
    found = find_crust()
    if found is None:
        raise SystemExit(
            "wasm_aot: crust with wasm2c not found beside this repository\n"
            "  git clone https://github.com/brentharts/crust.git "
            "../crust\n"
            "  (or set CRUST_DIR to a checkout that has tools/wasm2c.py)")
    return found


def c_ident(name: str) -> str:
    out = []
    for ch in name:
        out.append(ch if (ch.isalnum() or ch == "_") else "_")
    s = "".join(out)
    if not s or s[0].isdigit():
        s = "_" + s
    return s


def compile_c_modules(src_dir: Path, modules_dir: Path) -> list:
    """Compile `wasm/src/*.c` to `wasm/modules/*.wasm` with Crust.

    This is the first half of the round trip documented in wasm/README.md.
    Skipped when Crust has no wasm back end; an existing `.wasm` in
    `modules_dir` is left alone either way.
    """
    crust = find_crust()
    if crust is None or not src_dir.is_dir():
        return []

    modules_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    existing = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = (
        f"{crust}{os.pathsep}{existing}" if existing else str(crust))

    out = []
    for src in sorted(src_dir.glob("*.c")):
        wasm = modules_dir / (src.stem + ".wasm")
        if (wasm.exists()
                and wasm.stat().st_mtime >= src.stat().st_mtime):
            out.append(wasm)
            continue
        proc = subprocess.run(
            [sys.executable, "-m", "shivyc.main", "--target", "wasm",
             str(src), "-o", str(wasm)],
            capture_output=True, text=True, env=env, cwd=str(crust))
        if proc.returncode != 0:
            # A crust without --target wasm still serves wasm2c for
            # hand-dropped modules; do not fail the whole build.
            msg = (proc.stdout + proc.stderr).strip()
            print("  [wasm] could not compile %s:\n%s"
                  % (src.name, msg[:500]), file=sys.stderr)
            continue
        out.append(wasm)
    return out


def read_exports(wasm_path: Path):
    """(name, params, result) for each exported function.

    Read with Crust's own decoder rather than by re-parsing the binary here:
    one decoder, already tested against real modules, is better than a second
    partial one that can disagree with it.
    """
    sys.path.insert(0, str(crust_dir()))
    import shivyc.wasm as w
    import shivyc.wasm_reader as reader

    mod = reader.decode_file(str(wasm_path))
    out = []
    for exp in mod.exports:
        if exp.kind != w.EXTERNAL_KIND_FUNC:
            continue
        if exp.name in ("_start",):
            # The WASI entry point calls proc_exit, which would take the
            # browser down with it. Exports are called individually here.
            continue
        ft = mod.type_of_func(exp.index)
        params = ""
        ok = True
        for p in ft.params:
            if p not in KIND:
                ok = False
                break
            params += KIND[p]
        if not ok:
            continue
        if len(ft.results) > 1:
            continue
        result = KIND.get(ft.results[0], None) if ft.results else "v"
        if result is None:
            continue
        out.append((exp.name, params, result))
    return out


CTYPE = {"i": "unsigned int", "j": "unsigned long", "f": "float",
         "d": "double", "v": "void"}


def write_glue(module_name: str, exports, out_path: Path) -> None:
    """The registration glue for one module."""
    ident = c_ident(module_name)
    L = []
    a = L.append
    a("/* Registration glue for the AOT-compiled module '%s'." % module_name)
    a(" *")
    a(" * GENERATED by tools/wasm_aot.py. Do not edit; rebuild instead.")
    a(" *")
    a(" * The generated translation declares its exports as ordinary C")
    a(" * functions. This declares them again with the same signatures and")
    a(" * puts them in a table the registry can search by name. */")
    a('#include "wasm_registry.h"')
    a("")
    a("extern \"C\" {")
    for name, params, result in exports:
        args = ", ".join(CTYPE[p] for p in params) or "void"
        a("%s w2c_export_%s(%s);" % (CTYPE[result], c_ident(name), args))
    a("void wasm_init(void);")
    a("}")
    a("")
    a("namespace headless {")
    a("namespace {")
    a("")
    a("const WasmExport kExports[] = {")
    for name, params, result in exports:
        a('    { "%s", "%s", \'%s\', (void*) &w2c_export_%s },'
          % (name, params, result, c_ident(name)))
    if not exports:
        a('    { nullptr, "", \'v\', nullptr },')
    a("};")
    a("")
    a("const WasmModuleDef kModule = {")
    a('    "%s",' % module_name)
    a("    &wasm_init,")
    a("    kExports,")
    a("    %d" % len(exports))
    a("};")
    a("")
    a("/* Registration runs before main, so the module is visible to the")
    a(" * first page the browser loads. */")
    a("struct Registrar { Registrar() { wasmRegisterModule (&kModule); } };")
    a("const Registrar registrar;")
    a("")
    a("} // namespace")
    a("} // namespace headless")
    out_path.write_text("\n".join(L) + "\n")


def translate(wasm_path: Path, out_dir: Path) -> tuple:
    """Translate one module. Returns (module_c, glue_cpp)."""
    name = wasm_path.stem
    module_c = out_dir / ("wasm_%s.c" % c_ident(name))
    glue_cpp = out_dir / ("wasm_%s_glue.cpp" % c_ident(name))

    # Only retranslate when the input is newer, so an unchanged module does
    # not force a rebuild of a 45,000-line C file.
    if (module_c.exists() and glue_cpp.exists()
            and module_c.stat().st_mtime >= wasm_path.stat().st_mtime):
        return module_c, glue_cpp

    out_dir.mkdir(parents=True, exist_ok=True)
    crust = crust_dir()
    proc = subprocess.run(
        [sys.executable, str(crust / "tools" / "wasm2c.py"),
         str(wasm_path), "-o", str(module_c), "--no-main"],
        capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit("wasm_aot: could not translate %s:\n%s"
                         % (wasm_path.name, (proc.stdout + proc.stderr)[:800]))

    exports = read_exports(wasm_path)
    write_glue(name, exports, glue_cpp)
    return module_c, glue_cpp


def translate_all(modules_dir: Path, out_dir: Path,
                  src_dir: Path | None = None) -> list:
    """Translate every `.wasm` in `modules_dir`. Returns the source paths.

    When `src_dir` is set (or defaults to wasm/src beside modules), C sources
    there are compiled to `.wasm` first so a fresh checkout still gets the
    demo module without a hand-placed binary.
    """
    if not crust_available():
        return []

    if src_dir is None and modules_dir.name == "modules":
        candidate = modules_dir.parent / "src"
        if candidate.is_dir():
            src_dir = candidate
    if src_dir is not None:
        compile_c_modules(src_dir, modules_dir)

    if not modules_dir.is_dir():
        return []

    sources = []
    for wasm in sorted(modules_dir.glob("*.wasm")):
        module_c, glue_cpp = translate(wasm, out_dir)
        sources.append(module_c)
        sources.append(glue_cpp)
    return sources


def main(argv) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    if not crust_available():
        print("wasm_aot: crust with wasm2c not found")
        print("  git clone https://github.com/brentharts/crust.git beside "
              "this repository (or set CRUST_DIR)")
        return 1
    out = translate_all(Path(argv[1]), Path(argv[2]))
    for p in out:
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
