# mininodejs

A small node-like runner over the same [quickjs-ng](https://github.com/quickjs-ng/quickjs)
the browser links (vendored at 0.16.x).

```sh
./build.py mininodejs
./build/mininodejs/mininodejs script.js
./build/mininodejs/mininodejs -e 'console.log([1,2,3].map(x=>x*2))'
echo 'console.log(1+1)' | ./build/mininodejs/mininodejs
```

## Why

Reaching the JavaScript engine through a rendered page means a parser change
is checked by loading HTML, applying CSS and laying out a document first. That
is slow, and when it fails it tells you almost nothing. This runs a snippet and
prints what it did, and builds in about five seconds against thirty-odd for the
renderer.

It deliberately does not link litehtml. The engine is the same build the
browser uses, so what works here works there, but nothing about the page gets
in the way.

## Options

| | |
|---|---|
| `-e, --eval CODE` | Evaluate CODE instead of a file |
| `-c, --check` | Parse only, run nothing; exit 0 if it parses |
| `-m, --module` | Treat the source as an ES module |
| `--script` | Treat the source as a classic script |
| `-v, --version` | Print the engine version |

With no script, or with `-`, the source comes from stdin. Arguments after the
script appear in `process.argv`.

Module or script is guessed from the syntax when not forced, since quickjs has
to be told which before parsing and guessing wrong turns a valid file into a
syntax error. Top-level await needs module mode (`-m` or a source that looks
like a module).

`--check` is the useful one for engine work: it compiles without running, so a
parser gap can be told apart from a runtime one without side effects.

## What is provided

`console.log` and the `std` and `os` modules come from quickjs-libc. On top of
that: `console.error`, `warn`, `info` and `debug` (the error path goes to
stderr so a test can tell output from diagnostics), a minimal `process` with
`argv`, `platform`, `version` and `exit`, and `setTimeout`/`clearTimeout`
lifted from the `os` module onto the global object.

This is not node. There is no `require`, no `fs`, no networking. ES modules
work through quickjs's own loader.

## Testing

```sh
./test_mininodejs.py            # runner tests, then the feature matrix
./test_mininodejs.py --suite    # only the runner
./test_mininodejs.py --matrix   # only the matrix
./test_mininodejs.py -k class   # filter
```

The matrix runs each feature twice, once with `--check` and once for real,
which sorts failures into four kinds:

- **rejected by the parser** — work in the parser
- **missing a built-in** — the syntax is fine, some function does not exist;
  usually a small, self-contained addition
- **wrong result** — it parses and runs and the answer is wrong, which is the
  worst kind and worth chasing first
- **supported**

## Where it stands

quickjs-ng 0.16.x: **61 of 61 supported** on the feature matrix, including
static class blocks, private fields, BigInt, and top-level await (as an ES
module). The March 2021 Bellard snapshot this replaced left those two parser
gaps open; the rebase closed them.
