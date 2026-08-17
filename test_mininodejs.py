#!/usr/bin/env python3
"""Tests for mininodejs, and a survey of what its JavaScript engine supports.

Two things live here. The first is a normal suite over the runner itself --
arguments, stdin, exit codes, stderr. The second is a feature matrix: a list
of JavaScript features, each with a snippet that proves whether the engine
handles it, run twice, once to see whether it *parses* and once to see whether
it *behaves*.

That split matters. A feature the parser accepts but implements wrongly fails
differently from one it rejects outright, and while adding ES6+ support the
difference is the whole story: a parse failure is work in the parser, a
behaviour failure is work in the runtime.

Nothing is asserted about which features are present. The matrix reports, and
only the `EXPECTED_SUPPORTED` list at the bottom is enforced, so a regression
in something that used to work is a failure while an unimplemented feature is
just a row in a table.

Usage:
    ./test_mininodejs.py              run everything
    ./test_mininodejs.py --matrix     only the feature matrix
    ./test_mininodejs.py --suite      only the runner tests
    ./test_mininodejs.py --no-build
    ./test_mininodejs.py -k arrow     filter by name
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / "build" / "mininodejs" / "mininodejs"
WORK = Path("/tmp/mininodejs-tests")


# ---------------------------------------------------------------------------
# Feature matrix
#
# Each entry is (group, name, snippet). A snippet must print "ok" when the
# feature works, so that "runs but does the wrong thing" is distinguishable
# from "works".
#
# A snippet starting with "// module" is run as an ES module. Some syntax is
# only legal there -- top-level await most obviously -- and testing it as a
# script reports a parser gap that is really a harness mistake.
# ---------------------------------------------------------------------------

FEATURES = [
    # -- ES2015 ------------------------------------------------------------
    ("ES2015", "let/const", "let a=1; const b=2; { let a=3; } if(a===1&&b===2) console.log('ok')"),
    ("ES2015", "arrow functions", "const f=(x)=>x*2; if(f(21)===42) console.log('ok')"),
    ("ES2015", "template literals", "const n=6; if(`n=${n*7}`==='n=42') console.log('ok')"),
    ("ES2015", "classes", "class A{constructor(x){this.x=x} get double(){return this.x*2}} if(new A(21).double===42) console.log('ok')"),
    ("ES2015", "class inheritance", "class A{f(){return 1}} class B extends A{f(){return super.f()+1}} if(new B().f()===2) console.log('ok')"),
    ("ES2015", "destructuring", "const {a,b=2}={a:1}; const [x,,z]=[1,2,3]; if(a===1&&b===2&&x===1&&z===3) console.log('ok')"),
    ("ES2015", "default parameters", "function f(a=1,b=a+1){return a+b} if(f()===3) console.log('ok')"),
    ("ES2015", "rest/spread", "function f(...r){return r.length} const a=[1,2]; if(f(...a,3)===3) console.log('ok')"),
    ("ES2015", "for...of", "let s=0; for(const v of [1,2,3]) s+=v; if(s===6) console.log('ok')"),
    ("ES2015", "Map/Set", "const m=new Map([['a',1]]); const s=new Set([1,1,2]); if(m.get('a')===1&&s.size===2) console.log('ok')"),
    ("ES2015", "Symbol", "const s=Symbol('x'); const o={[s]:1}; if(o[s]===1) console.log('ok')"),
    ("ES2015", "Promise", "Promise.resolve(42).then(v=>{ if(v===42) console.log('ok') })"),
    ("ES2015", "generators", "function* g(){yield 1; yield 2} const a=[...g()]; if(a[1]===2) console.log('ok')"),
    ("ES2015", "computed properties", "const k='a'; const o={[k+'b']:1}; if(o.ab===1) console.log('ok')"),
    ("ES2015", "shorthand methods", "const a=1; const o={a,f(){return this.a}}; if(o.f()===1) console.log('ok')"),
    ("ES2015", "tagged templates", "function t(s,...v){return s[0]+v[0]} if(t`a${1}b`==='a1') console.log('ok')"),
    ("ES2015", "Proxy/Reflect", "const p=new Proxy({},{get:()=>42}); if(p.anything===42&&Reflect.has({a:1},'a')) console.log('ok')"),

    # -- ES2016/2017 -------------------------------------------------------
    ("ES2016+", "exponent operator", "if(2**10===1024) console.log('ok')"),
    ("ES2016+", "Array.includes", "if([1,2].includes(2)) console.log('ok')"),
    ("ES2016+", "async/await", "(async()=>{const v=await Promise.resolve(42); if(v===42) console.log('ok')})()"),
    ("ES2016+", "Object.entries/values", "if(Object.entries({a:1})[0][1]===1&&Object.values({a:1})[0]===1) console.log('ok')"),
    ("ES2016+", "string padStart/padEnd", "if('5'.padStart(3,'0')==='005') console.log('ok')"),
    ("ES2016+", "trailing commas in args", "function f(a,b,){return a+b} if(f(1,2,)===3) console.log('ok')"),

    # -- ES2018/2019 -------------------------------------------------------
    ("ES2018+", "object spread/rest", "const {a,...r}={a:1,b:2,c:3}; const o={...r,d:4}; if(a===1&&o.b===2&&o.d===4) console.log('ok')"),
    ("ES2018+", "async iteration", "(async()=>{async function* g(){yield 1;yield 2} let s=0; for await(const v of g()) s+=v; if(s===3) console.log('ok')})()"),
    ("ES2018+", "Promise.finally", "Promise.resolve(1).finally(()=>{}).then(v=>{if(v===1)console.log('ok')})"),
    ("ES2018+", "regex named groups", "const m=/(?<y>\\d{4})/.exec('2026'); if(m.groups.y==='2026') console.log('ok')"),
    ("ES2018+", "Array.flat/flatMap", "if([1,[2,[3]]].flat(2)[2]===3&&[1,2].flatMap(x=>[x,x])[3]===2) console.log('ok')"),
    ("ES2018+", "Object.fromEntries", "if(Object.fromEntries([['a',1]]).a===1) console.log('ok')"),
    ("ES2018+", "String.trimStart/End", "if('  a '.trimStart()==='a ') console.log('ok')"),
    ("ES2018+", "optional catch binding", "try{null.x}catch{console.log('ok')}"),

    # -- ES2020 ------------------------------------------------------------
    ("ES2020", "optional chaining", "const o={a:{b:1}}; if(o?.a?.b===1&&o?.x?.y===undefined) console.log('ok')"),
    ("ES2020", "nullish coalescing", "const a=null??42; const b=0??7; if(a===42&&b===0) console.log('ok')"),
    ("ES2020", "BigInt", "if(2n**64n===18446744073709551616n) console.log('ok')"),
    ("ES2020", "globalThis", "if(typeof globalThis==='object') console.log('ok')"),
    ("ES2020", "Promise.allSettled", "Promise.allSettled([Promise.resolve(1)]).then(r=>{if(r[0].status==='fulfilled')console.log('ok')})"),
    ("ES2020", "String.matchAll", "if([...'aa'.matchAll(/a/g)].length===2) console.log('ok')"),
    ("ES2020", "dynamic import()", "import('os').then(()=>console.log('ok')).catch(()=>{})"),

    # -- ES2021+ -----------------------------------------------------------
    ("ES2021+", "logical assignment", "let a=null; a??=1; let b=1; b||=2; let c=1; c&&=3; if(a===1&&b===1&&c===3) console.log('ok')"),
    ("ES2021+", "numeric separators", "if(1_000_000===1000000) console.log('ok')"),
    ("ES2021+", "String.replaceAll", "if('aa'.replaceAll('a','b')==='bb') console.log('ok')"),
    ("ES2021+", "Promise.any", "Promise.any([Promise.resolve(1)]).then(v=>{if(v===1)console.log('ok')})"),
    ("ES2021+", "class fields", "class A{x=1; #p=2; getP(){return this.#p}} const a=new A(); if(a.x===1&&a.getP()===2) console.log('ok')"),
    ("ES2021+", "static class blocks", "class A{static x; static{A.x=1}} if(A.x===1) console.log('ok')"),
    ("ES2021+", "Array.at", "if([1,2,3].at(-1)===3) console.log('ok')"),
    ("ES2021+", "Object.hasOwn", "if(Object.hasOwn({a:1},'a')) console.log('ok')"),
    ("ES2021+", "Array.findLast", "if([1,2,3].findLast(x=>x<3)===2) console.log('ok')"),
    ("ES2021+", "top-level await", "// module\nconst v = await Promise.resolve(1); if(v===1) console.log('ok')"),
    ("ES2021+", "import.meta", "// module\nif(typeof import.meta==='object') console.log('ok')"),
    ("ES2021+", "structuredClone", "const o=structuredClone({a:1}); if(o.a===1) console.log('ok')"),

    # -- Things the browser side will lean on -------------------------------
    ("runtime", "JSON round trip", "const o=JSON.parse(JSON.stringify({a:[1,{b:2}]})); if(o.a[1].b===2) console.log('ok')"),
    ("runtime", "RegExp lookbehind", "if(/(?<=a)b/.test('ab')) console.log('ok')"),
    ("runtime", "Unicode strings", "if('日本語'.length===3&&[...'😀'].length===1) console.log('ok')"),
    ("runtime", "Date", "if(new Date(0).getUTCFullYear()===1970) console.log('ok')"),
    ("runtime", "TypedArray", "const a=new Uint8Array([1,2]); if(a[1]===2) console.log('ok')"),
    ("runtime", "WeakMap", "const k={}; const m=new WeakMap([[k,1]]); if(m.get(k)===1) console.log('ok')"),
    ("runtime", "getter/setter", "const o={_v:0,get v(){return this._v},set v(x){this._v=x*2}}; o.v=21; if(o.v===42) console.log('ok')"),
    ("runtime", "labelled break", "outer: for(let i=0;i<3;i++){for(let j=0;j<3;j++){if(j===1) continue outer; if(i===2) break outer}} console.log('ok')"),
]

# Features known to work. A failure here is a regression and fails the run;
# anything else is reported but tolerated.
EXPECTED_SUPPORTED = {
    "let/const", "arrow functions", "template literals", "classes",
    "class inheritance", "destructuring", "default parameters", "rest/spread",
    "for...of", "Map/Set", "Symbol", "Promise", "generators",
    "computed properties", "shorthand methods", "tagged templates",
    "exponent operator", "Array.includes", "async/await",
    "Object.entries/values", "string padStart/padEnd", "object spread/rest",
    "optional chaining", "nullish coalescing", "globalThis",
    "JSON round trip", "Unicode strings", "Date", "TypedArray", "WeakMap",
    "getter/setter", "labelled break", "optional catch binding",
}


def run(args, stdin=None, timeout=20):
    proc = subprocess.run([str(BINARY)] + args, capture_output=True, text=True,
                          input=stdin, timeout=timeout)
    return proc.returncode, proc.stdout, proc.stderr


def probe(snippet):
    """Classifies a feature.

    'ok'      the snippet parsed and produced the right answer
    'missing' the syntax is fine but a built-in it needs is absent
    'runtime' it parsed and ran, but the answer was wrong
    'parse'   the parser rejected it

    Separating 'missing' from 'runtime' is worth the extra check: a missing
    built-in is a small, self-contained addition, while a wrong answer means
    something already implemented is not behaving.
    """
    mode = ["-m"] if snippet.startswith("// module") else []

    rc, out, err = run(mode + ["--check", "-e", snippet])
    if rc != 0:
        return "parse"

    rc, out, err = run(mode + ["-e", snippet])
    if rc == 0 and out.strip() == "ok":
        return "ok"

    if "not a function" in err or "not defined" in err or "undefined" in err:
        return "missing"

    return "runtime"


# ---------------------------------------------------------------------------
# Runner suite
# ---------------------------------------------------------------------------

def suite(filter_text):
    WORK.mkdir(parents=True, exist_ok=True)

    script = WORK / "args.js"
    script.write_text("console.log('args:'+process.argv.slice(2).join(','));\n")

    module = WORK / "mod.mjs"
    module.write_text("export const answer = 42;\n")

    importer = WORK / "importer.mjs"
    importer.write_text(
        "import { answer } from './mod.mjs';\nconsole.log('answer', answer);\n")

    checks = []

    def check(name, cond, detail=""):
        if filter_text and filter_text not in name:
            return
        checks.append((name, cond, detail))

    rc, out, err = run(["--version"])
    check("prints a version", rc == 0 and "quickjs" in out, out.strip())

    rc, out, err = run(["-e", "console.log(1+1)"])
    check("evaluates with -e", rc == 0 and out.strip() == "2", (out + err).strip())

    rc, out, err = run([], stdin="console.log('from stdin')")
    check("reads stdin", rc == 0 and "from stdin" in out, (out + err).strip())

    rc, out, err = run([str(script), "a", "b"])
    check("passes script arguments", rc == 0 and "args:a,b" in out,
          (out + err).strip())

    rc, out, err = run(["-e", "console.log('out'); console.error('err')"])
    check("separates stdout and stderr",
          out.strip() == "out" and "err" in err, repr(out) + repr(err))

    rc, out, err = run(["-e", "throw new Error('boom')"])
    check("exits non-zero on an uncaught throw", rc != 0 and "boom" in err,
          err.strip())

    rc, out, err = run(["-e", "process.exit(3)"])
    check("honours process.exit", rc == 3, str(rc))

    rc, out, err = run(["--check", "-e", "function ( {"])
    check("rejects bad syntax with --check", rc != 0, out.strip())

    rc, out, err = run(["--check", "-e", "const a = 1;"])
    check("accepts good syntax with --check", rc == 0, err.strip())

    rc, out, err = run(["--check", "-e", "console.log('should not run')"])
    check("--check runs nothing", rc == 0 and out.strip() == "", out.strip())

    rc, out, err = run([str(importer)])
    check("imports an ES module", rc == 0 and "answer 42" in out,
          (out + err).strip())

    rc, out, err = run(["-e",
                        "setTimeout(()=>console.log('timer'),1); console.log('sync')"])
    check("runs timers after the script",
          rc == 0 and out.split() == ["sync", "timer"], repr(out))

    rc, out, err = run(["-e",
                        "(async()=>{await 0; console.log('job')})(); console.log('main')"])
    check("drains promise jobs",
          rc == 0 and out.split() == ["main", "job"], repr(out))

    rc, out, err = run(["/does/not/exist.js"])
    check("reports a missing script", rc != 0 and "cannot open" in err,
          err.strip())

    return checks


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--matrix", action="store_true", help="only the feature matrix")
    ap.add_argument("--suite", action="store_true", help="only the runner tests")
    ap.add_argument("-k", metavar="PATTERN", help="filter by name")
    args = ap.parse_args()

    if not args.no_build:
        if subprocess.run([sys.executable, str(ROOT / "build.py"),
                           "mininodejs"]).returncode != 0:
            return 1
        print()

    if not BINARY.exists():
        print("error: build the mininodejs target first", file=sys.stderr)
        return 1

    failures = 0
    run_suite = not args.matrix
    run_matrix = not args.suite

    if run_suite:
        print("Runner")
        for name, ok, detail in suite(args.k):
            print(f"  {'ok  ' if ok else 'FAIL'}  {name}")
            if not ok:
                failures += 1
                if detail:
                    print(f"        {detail}")
        print()

    if run_matrix:
        print("JavaScript feature matrix")
        group = None
        counts = {"ok": 0, "missing": 0, "runtime": 0, "parse": 0}
        regressions = []

        for grp, name, snippet in FEATURES:
            if args.k and args.k not in name:
                continue

            if grp != group:
                group = grp
                print(f"\n  {grp}")

            state = probe(snippet)
            counts[state] += 1

            mark = {"ok": "yes", "missing": "  ~", "runtime": "  ~",
                    "parse": "  no"}[state]
            note = {"ok": "", "missing": "  (syntax fine, built-in absent)",
                    "runtime": "  (runs, wrong result)",
                    "parse": "  (syntax error)"}[state]
            print(f"    {mark:>3}  {name}{note}")

            if state != "ok" and name in EXPECTED_SUPPORTED:
                regressions.append(name)

        total = sum(counts.values())
        print(f"\n  {counts['ok']}/{total} supported, "
              f"{counts['missing']} missing a built-in, "
              f"{counts['runtime']} wrong result, "
              f"{counts['parse']} rejected by the parser")

        if regressions:
            print("\n  REGRESSIONS (these used to work):")
            for name in regressions:
                print(f"    {name}")
            failures += len(regressions)

    print()
    if failures:
        print(f"{failures} failure(s)")
        return 1

    print("all good")
    return 0


if __name__ == "__main__":
    sys.exit(main())
