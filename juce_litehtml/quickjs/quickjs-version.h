/* Compatibility shim for code that still expects QUICKJS_VERSION.
   quickjs-ng defines QJS_VERSION_MAJOR/MINOR/PATCH in quickjs.h. */
#ifndef QUICKJS_VERSION_H
#define QUICKJS_VERSION_H
#include "quickjs.h"
#define _QJS_VER_STR_HELPER(x) #x
#define _QJS_VER_STR(x) _QJS_VER_STR_HELPER(x)
#ifndef QUICKJS_VERSION
#define QUICKJS_VERSION \
    _QJS_VER_STR(QJS_VERSION_MAJOR) "." \
    _QJS_VER_STR(QJS_VERSION_MINOR) "." \
    _QJS_VER_STR(QJS_VERSION_PATCH) QJS_VERSION_SUFFIX
#endif
#endif
