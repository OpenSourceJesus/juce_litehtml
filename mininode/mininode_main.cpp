#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "quickjs.h"
#include "quickjs-libc.h"

#include "../headless/crust_compat.h"
#include "../headless/js_prelude.h"
#ifdef HEADLESS_WASM
#include "wasm_js.h"
#endif

// A small node-like runner over the quickjs the browser already vendors.
//
// It exists to make the JavaScript side testable on its own. Reaching the
// engine through a rendered page means a parser change is checked by loading
// HTML, applying CSS and laying out a document first, which is slow and tells
// you very little when it fails. This runs a snippet and prints what it did.
//
// The engine is the same build the browser links, so what works here works
// there.

static const char* kVersion = "mininodejs 0.1 (quickjs " QUICKJS_VERSION ")";

//==============================================================================

/** console.error / console.warn: the same formatting as console.log, but to
    stderr, so a test can tell output from diagnostics.
 */
static JSValue jsPrintToStderr (JSContext* ctx, JSValueConst thisVal,
                                int argc, JSValueConst* argv)
{
    int i = 0;

    while (i < argc)
    {
        if (i != 0)
            fputc (' ', stderr);

        size_t len = 0;
        const char* str = JS_ToCStringLen (ctx, &len, argv[i]);

        if (str == 0)
            return JS_EXCEPTION;

        fwrite (str, 1, len, stderr);
        JS_FreeCString (ctx, str);

        i = i + 1;
    }

    fputc ('\n', stderr);
    return JS_UNDEFINED;
}

/** process.exit(code) */
static JSValue jsProcessExit (JSContext* ctx, JSValueConst thisVal,
                              int argc, JSValueConst* argv)
{
    int code = 0;

    if (argc > 0)
        JS_ToInt32 (ctx, &code, argv[0]);

    exit (code);
    return JS_UNDEFINED;
}

/** Installs the handful of globals a node-shaped script expects. */
static void installGlobals (JSContext* ctx, const char* scriptName,
                            int argc, char** argv)
{
    JSValue global = JS_GetGlobalObject (ctx);

#ifdef HEADLESS_WASM
    // WebAssembly modules compiled into this binary ahead of time. Present
    // only when the build bundled some; a build without them has no
    // WebAssembly object at all, which is what a page should feature-detect.
    headless::installWasmBindings (ctx, global);
#endif

    // console.log and print come from quickjs-libc; the rest do not.
    JSValue console = JS_GetPropertyStr (ctx, global, "console");

    if (JS_IsObject (console))
    {
        JS_SetPropertyStr (ctx, console, "error",
                           JS_NewCFunction (ctx, jsPrintToStderr, "error", 1));
        JS_SetPropertyStr (ctx, console, "warn",
                           JS_NewCFunction (ctx, jsPrintToStderr, "warn", 1));

        JSValue logFn = JS_GetPropertyStr (ctx, console, "log");
        JS_SetPropertyStr (ctx, console, "info", JS_DupValue (ctx, logFn));
        JS_SetPropertyStr (ctx, console, "debug", logFn);
    }

    JS_FreeValue (ctx, console);

    // A minimal process object. Not node's, just enough that a test script
    // can find its arguments and choose an exit code.
    JSValue process = JS_NewObject (ctx);

    JSValue args = JS_NewArray (ctx);
    JS_SetPropertyUint32 (ctx, args, 0, JS_NewString (ctx, "mininodejs"));
    JS_SetPropertyUint32 (ctx, args, 1,
                          JS_NewString (ctx, (scriptName != 0) ? scriptName : "-"));

    int i = 0;

    while (i < argc)
    {
        JS_SetPropertyUint32 (ctx, args, (uint32_t) (i + 2),
                              JS_NewString (ctx, argv[i]));
        i = i + 1;
    }

    JS_SetPropertyStr (ctx, process, "argv", args);
    JS_SetPropertyStr (ctx, process, "platform", JS_NewString (ctx, "linux"));
    JS_SetPropertyStr (ctx, process, "version", JS_NewString (ctx, kVersion));
    JS_SetPropertyStr (ctx, process, "exit",
                       JS_NewCFunction (ctx, jsProcessExit, "exit", 1));

    JS_SetPropertyStr (ctx, global, "process", process);

    JS_FreeValue (ctx, global);
}

/** Installs the standard library functions this quickjs predates. */
static void installPrelude (JSContext* ctx)
{
    const char* prelude = headless::jsPrelude();

    JSValue result = JS_Eval (ctx, prelude, strlen (prelude), "<prelude>",
                              JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException (result))
    {
        // A broken prelude is a bug here, not in the user's script, so say so
        // rather than letting it look like their code failed.
        fprintf (stderr, "internal error: the JS prelude failed to load\n");
        js_std_dump_error (ctx);
    }

    JS_FreeValue (ctx, result);
}

/** Timers live on the os module; a script expects them as globals. */
static void installTimers (JSContext* ctx)
{
    const char* prelude =
        "import * as os from 'os';\n"
        "globalThis.setTimeout = os.setTimeout;\n"
        "globalThis.clearTimeout = os.clearTimeout;\n";

    JSValue result = JS_Eval (ctx, prelude, strlen (prelude), "<timers>",
                              JS_EVAL_TYPE_MODULE);

    // Not fatal: a script that never sets a timer does not care.
    if (JS_IsException (result))
        JS_FreeValue (ctx, JS_GetException (ctx));

    JS_FreeValue (ctx, result);
}

//==============================================================================

static int readStream (FILE* in, std::string* out)
{
    char buffer[8192];
    size_t n = fread (buffer, 1, 8192, in);

    while (n > 0)
    {
        int k = 0;

        while (k < (int) n)
        {
            out->push_back (buffer[k]);
            k = k + 1;
        }

        n = fread (buffer, 1, 8192, in);
    }

    return 1;
}

/** True when the source looks like an ES module.

    quickjs has to be told which it is before parsing, and getting it wrong
    turns a valid file into a syntax error, so guess from the syntax that only
    a module may contain.
 */
static int looksLikeModule (const char* source)
{
    int i = 0;
    int atLineStart = 1;

    while (source[i] != '\0')
    {
        if (atLineStart != 0)
        {
            // Skip indentation.
            while (source[i] == ' ' || source[i] == '\t')
                i = i + 1;

            if (strncmp (source + i, "import ", 7) == 0
                || strncmp (source + i, "import(", 7) == 0
                || strncmp (source + i, "export ", 7) == 0
                || strncmp (source + i, "export{", 7) == 0
                || strncmp (source + i, "export default", 14) == 0)
                return 1;
        }

        atLineStart = (source[i] == '\n') ? 1 : 0;

        if (source[i] != '\0')
            i = i + 1;
    }

    return 0;
}

static void printUsage (const char* argv0)
{
    printf ("%s\n\n", kVersion);
    printf ("Usage: %s [options] [script.js] [args...]\n\n", argv0);
    printf ("Runs a script with the same JavaScript engine the browser uses.\n\n");
    printf ("Options:\n");
    printf ("  -e, --eval CODE   Evaluate CODE instead of a file\n");
    printf ("  -c, --check       Parse only, run nothing; exit 0 if it parses\n");
    printf ("  -m, --module      Treat the source as an ES module\n");
    printf ("      --script      Treat the source as a classic script\n");
    printf ("  -v, --version     Print the version\n");
    printf ("      --help        Show this message\n\n");
    printf ("With no script, or with '-', the source is read from stdin.\n");
    printf ("Arguments after the script are visible as process.argv.\n");
}

int main (int argc, char** argv)
{
    std::string source;
    std::string scriptName;
    int haveSource = 0;
    int checkOnly = 0;
    int forceModule = 0;
    int forceScript = 0;
    int scriptArgStart = argc;

    int i = 1;

    while (i < argc)
    {
        const char* arg = argv[i];
        const int hasNext = (i + 1 < argc) ? 1 : 0;

        if (strcmp (arg, "--help") == 0)
        {
            printUsage (argv[0]);
            return 0;
        }
        else if (strcmp (arg, "-v") == 0 || strcmp (arg, "--version") == 0)
        {
            printf ("%s\n", kVersion);
            return 0;
        }
        else if (strcmp (arg, "-e") == 0 || strcmp (arg, "--eval") == 0)
        {
            if (hasNext == 0) { fprintf (stderr, "error: %s needs code\n", arg); return 2; }
            i = i + 1;
            source.assign (argv[i]);
            scriptName.assign ("<eval>");
            haveSource = 1;
        }
        else if (strcmp (arg, "-c") == 0 || strcmp (arg, "--check") == 0)
        {
            checkOnly = 1;
        }
        else if (strcmp (arg, "-m") == 0 || strcmp (arg, "--module") == 0)
        {
            forceModule = 1;
        }
        else if (strcmp (arg, "--script") == 0)
        {
            forceScript = 1;
        }
        else if (arg[0] == '-' && strcmp (arg, "-") != 0)
        {
            fprintf (stderr, "error: unknown option %s\n", arg);
            return 2;
        }
        else
        {
            // The first bare argument is the script; the rest are its own.
            if (haveSource == 0)
            {
                if (strcmp (arg, "-") == 0)
                {
                    readStream (stdin, &source);
                    scriptName.assign ("<stdin>");
                }
                else
                {
                    FILE* f = fopen (arg, "rb");

                    if (f == 0)
                    {
                        fprintf (stderr, "error: cannot open %s\n", arg);
                        return 1;
                    }

                    readStream (f, &source);
                    fclose (f);
                    scriptName.assign (arg);
                }

                haveSource = 1;
            }

            scriptArgStart = i + 1;
            i = argc;
            continue;
        }

        i = i + 1;
    }

    if (haveSource == 0)
    {
        readStream (stdin, &source);
        scriptName.assign ("<stdin>");
    }

    JSRuntime* rt = JS_NewRuntime();

    if (rt == 0)
    {
        fprintf (stderr, "error: cannot create a JS runtime\n");
        return 1;
    }

    js_std_init_handlers (rt);
    JS_SetModuleLoaderFunc (rt, 0, js_module_loader, 0);

    JSContext* ctx = JS_NewContext (rt);

    if (ctx == 0)
    {
        fprintf (stderr, "error: cannot create a JS context\n");
        JS_FreeRuntime (rt);
        return 1;
    }

    js_init_module_std (ctx, "std");
    js_init_module_os (ctx, "os");

    int scriptArgc = argc - scriptArgStart;

    if (scriptArgc < 0)
        scriptArgc = 0;

    js_std_add_helpers (ctx, scriptArgc, argv + scriptArgStart);
    installGlobals (ctx, scriptName.c_str(), scriptArgc, argv + scriptArgStart);
    installPrelude (ctx);
    installTimers (ctx);

    int flags = 0;

    if (forceModule != 0)
        flags = JS_EVAL_TYPE_MODULE;
    else if (forceScript != 0)
        flags = JS_EVAL_TYPE_GLOBAL;
    else if (looksLikeModule (source.c_str()) != 0)
        flags = JS_EVAL_TYPE_MODULE;
    else
        flags = JS_EVAL_TYPE_GLOBAL;

    if (checkOnly != 0)
        flags = flags | JS_EVAL_FLAG_COMPILE_ONLY;

    JSValue result = JS_Eval (ctx, source.c_str(), source.size(),
                              scriptName.c_str(), flags);

    int status = 0;

    if (JS_IsException (result))
    {
        js_std_dump_error (ctx);
        status = 1;
    }

    // Drain before releasing the result. A module compiled as an async
    // function returns a promise that is still pending at the first
    // top-level await, and dropping the last reference to it before the jobs
    // run leaves the continuation with nothing to resume.
    if (status == 0 && checkOnly == 0)
        js_std_loop (ctx);

    JS_FreeValue (ctx, result);

    js_std_free_handlers (rt);
    JS_FreeContext (ctx);
    JS_FreeRuntime (rt);

    return status;
}
