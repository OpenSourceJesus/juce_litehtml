# mininodejs

A small node-like runner over the same quickjs the browser links.

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
syntax error.

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

Only the `EXPECTED_SUPPORTED` list is enforced. Something on it breaking is a
failure; an unimplemented feature is a row in a table, not an error. That way
the matrix can describe the engine honestly without the suite going red over
work not yet done.

## The prelude

`headless/js_prelude.h` defines the standard library functions this quickjs
predates: `Array.prototype.at`, `String.prototype.at`, `findLast`,
`findLastIndex`, `Object.hasOwn` and `structuredClone`.

They live in JavaScript because that is all they are -- quickjs parses every
one of them fine, the functions simply do not exist, and a C implementation
would be more code for the same result. They live in `headless/` rather than
here because the browser's script context needs the same additions: a page
using `Array.at` should not behave differently from a snippet using it.

Every definition is guarded with an `in` check, so rebasing onto a quickjs
that implements them natively silently stops using the prelude rather than
overwriting better implementations.

`structuredClone` handles plain objects, arrays, `Map`, `Set`, `Date`,
`RegExp`, `ArrayBuffer` and typed arrays, preserves cycles, and throws on a
function as the real one does. It is not the full algorithm -- there is no
transfer list.

## Top-level await: why it is not a small patch

Worth writing down, because it looks like a two-line change and is not.

`await` at a module's top level is rejected by two guards, and both come off
easily. Compiling the module body as an async function (`fd->func_kind =
JS_FUNC_ASYNC`) satisfies the first, and setting `fd->in_function_body`
satisfies the second -- the latter is correct in principle, since that flag
exists only to stop `await` appearing while an async function's *argument
defaults* are parsed, and a module has no argument list.

With both changed the module parses and runs to its first `await`. Then it
stops. No job is queued, the runtime asserts on shutdown with objects still
live, and the value the evaluation returns is neither a promise nor
`undefined` but an internal sentinel.

The reason is that `js_evaluate_module` calls the module's function object
directly. An async function is only driven correctly when its call goes
through `js_async_function_call`, which the class dispatch table wires up for
`JS_CLASS_ASYNC_FUNCTION` objects -- and the module's function object is not
one. So the interpreter reaches `OP_await`, returns its internal "I have
suspended" value to a caller that has no idea what to do with it, and the
suspended state is dropped on the floor.

Making this work means driving the module through the async machinery, and
then dealing with what that implies: a module that suspends has not finished
when `js_evaluate_module` returns, so every module that imports it has to wait
for it, which is the async module graph the specification describes. That is a
real feature, not a patch.

**The recommendation is to rebase quickjs rather than implement this.**
Upstream added top-level await in 2023, along with static class blocks -- the
other parser gap here -- so a rebase closes both, and the prelude above
disappears with them. The vendored copy is from March 2021.

## Where it stands

quickjs 2021-03-27 plus the prelude, at the time of writing: **59 of 61
supported, nothing missing a built-in, 0 wrong results.**

ES2015 through ES2020 are essentially complete, including classes, generators,
async/await, async iteration, Proxy/Reflect, BigInt, optional chaining,
nullish coalescing, named capture groups and lookbehind.

What remains missing is the two parser gaps, and only those:

| Gap | Kind |
|---|---|
| static class blocks | parser |
| top-level await | parser |

Both are real engine work rather than additions, and both are implemented
upstream. Top-level await is the one that matters, since a bundler targeting
modern browsers will emit it.
