#include "wasm_registry.h"

namespace headless {

// A fixed table rather than a vector: registration happens from static
// initialisers, before anything has had a chance to run, and a container with
// a constructor of its own would introduce an initialisation-order question
// for no benefit. Sixteen is far more modules than a browser build bundles.
static const int kMaxModules = 16;
static const WasmModuleDef* gModules[kMaxModules];
static int gModuleCount = 0;
static bool gInitialised[kMaxModules];

void wasmRegisterModule (const WasmModuleDef* def)
{
    if (def == nullptr || gModuleCount >= kMaxModules)
        return;

    gModules[gModuleCount] = def;
    gInitialised[gModuleCount] = false;
    gModuleCount++;
}

int wasmModuleCount()
{
    return gModuleCount;
}

const WasmModuleDef* wasmModuleAt (int index)
{
    if (index < 0 || index >= gModuleCount)
        return nullptr;

    return gModules[index];
}

static bool sameString (const char* a, const char* b)
{
    if (a == nullptr || b == nullptr)
        return false;

    int i = 0;

    while (a[i] != 0 && b[i] != 0)
    {
        if (a[i] != b[i])
            return false;

        i++;
    }

    return a[i] == b[i];
}

const WasmModuleDef* wasmFindModule (const char* name)
{
    for (int i = 0; i < gModuleCount; ++i)
        if (sameString (gModules[i]->name, name))
            return gModules[i];

    return nullptr;
}

const WasmExport* wasmFindExport (const WasmModuleDef* mod, const char* name)
{
    if (mod == nullptr)
        return nullptr;

    for (int i = 0; i < mod->exportCount; ++i)
        if (sameString (mod->exports[i].name, name))
            return &mod->exports[i];

    return nullptr;
}

void wasmEnsureInit (const WasmModuleDef* mod)
{
    for (int i = 0; i < gModuleCount; ++i)
    {
        if (gModules[i] != mod)
            continue;

        if (! gInitialised[i])
        {
            gInitialised[i] = true;

            if (mod->init != nullptr)
                mod->init();
        }

        return;
    }
}

/*  Calling an export.

    The generated functions have real C signatures, so calling one means
    casting the stored pointer back to the right shape. There is no portable
    way to build a call from a runtime description, so the shapes actually
    used are enumerated. Anything outside them is refused rather than
    guessed at: a wrong cast here would not fail visibly, it would read
    whatever happened to be in the argument registers.

    Only i32 and f64 parameters are handled, in arities up to four. That
    covers every export this browser bundles; extending it is adding cases,
    and an unsupported shape reports itself.
 */
static int paramCount (const char* params)
{
    int n = 0;

    while (params != nullptr && params[n] != 0)
        n++;

    return n;
}

static bool allInt (const char* params, int n)
{
    for (int i = 0; i < n; ++i)
        if (params[i] != 'i')
            return false;

    return true;
}

static bool allDouble (const char* params, int n)
{
    for (int i = 0; i < n; ++i)
        if (params[i] != 'd')
            return false;

    return true;
}

typedef unsigned int u32;

typedef u32 (*Fn_i_0)();
typedef u32 (*Fn_i_1)(u32);
typedef u32 (*Fn_i_2)(u32, u32);
typedef u32 (*Fn_i_3)(u32, u32, u32);
typedef u32 (*Fn_i_4)(u32, u32, u32, u32);

typedef void (*Fn_v_0)();
typedef void (*Fn_v_1)(u32);
typedef void (*Fn_v_2)(u32, u32);
typedef void (*Fn_v_3)(u32, u32, u32);
typedef void (*Fn_v_4)(u32, u32, u32, u32);

typedef double (*Fn_d_0)();
typedef double (*Fn_d_1)(double);
typedef double (*Fn_d_2)(double, double);

double wasmCallExport (const WasmModuleDef* mod, const WasmExport* fn,
                       const double* args, int argCount, bool* ok)
{
    if (ok != nullptr)
        *ok = false;

    if (mod == nullptr || fn == nullptr || fn->fn == nullptr)
        return 0.0;

    const int n = paramCount (fn->params);

    if (n != argCount)
        return 0.0;

    wasmEnsureInit (mod);

    // All-i32 parameters: the common case by a wide margin, since that is
    // what a C function compiled to wasm32 looks like.
    if (allInt (fn->params, n))
    {
        u32 a[4];

        for (int i = 0; i < n && i < 4; ++i)
            a[i] = (u32) (long long) args[i];

        if (fn->result == 'i')
        {
            if (n == 0) { if (ok) *ok = true; return (double) (int) ((Fn_i_0) fn->fn)(); }
            if (n == 1) { if (ok) *ok = true; return (double) (int) ((Fn_i_1) fn->fn)(a[0]); }
            if (n == 2) { if (ok) *ok = true; return (double) (int) ((Fn_i_2) fn->fn)(a[0], a[1]); }
            if (n == 3) { if (ok) *ok = true; return (double) (int) ((Fn_i_3) fn->fn)(a[0], a[1], a[2]); }
            if (n == 4) { if (ok) *ok = true; return (double) (int) ((Fn_i_4) fn->fn)(a[0], a[1], a[2], a[3]); }
        }
        else if (fn->result == 'v')
        {
            if (n == 0) { ((Fn_v_0) fn->fn)(); if (ok) *ok = true; return 0.0; }
            if (n == 1) { ((Fn_v_1) fn->fn)(a[0]); if (ok) *ok = true; return 0.0; }
            if (n == 2) { ((Fn_v_2) fn->fn)(a[0], a[1]); if (ok) *ok = true; return 0.0; }
            if (n == 3) { ((Fn_v_3) fn->fn)(a[0], a[1], a[2]); if (ok) *ok = true; return 0.0; }
            if (n == 4) { ((Fn_v_4) fn->fn)(a[0], a[1], a[2], a[3]); if (ok) *ok = true; return 0.0; }
        }

        return 0.0;
    }

    // All-f64 parameters, for a module doing floating point.
    if (allDouble (fn->params, n) && fn->result == 'd')
    {
        if (n == 0) { if (ok) *ok = true; return ((Fn_d_0) fn->fn)(); }
        if (n == 1) { if (ok) *ok = true; return ((Fn_d_1) fn->fn)(args[0]); }
        if (n == 2) { if (ok) *ok = true; return ((Fn_d_2) fn->fn)(args[0], args[1]); }
    }

    return 0.0;
}

} // namespace headless
