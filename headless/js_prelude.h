#pragma once

namespace headless {

/** A small prelude of standard library functions the vendored quickjs
    predates.

    These are pure library additions, not syntax: quickjs parses every one of
    them fine, the functions simply do not exist. That makes JavaScript the
    right place to define them -- there is nothing here the engine needs to
    know about, and a C implementation would be more code for the same result.

    Kept in headless/ rather than in mininode/ because the browser's script
    context needs exactly the same additions; a page using Array.at should not
    behave differently from a snippet using it.

    Everything is guarded, so rebasing onto a newer quickjs that implements
    these natively silently stops using them rather than overwriting them.
 */
inline const char* jsPrelude()
{
    return
    "(function(){\n"
    "  'use strict';\n"
    "\n"
    "  function def(obj, name, value) {\n"
    "    if (obj && !(name in obj)) {\n"
    "      Object.defineProperty(obj, name, {\n"
    "        value: value, writable: true, enumerable: false, configurable: true\n"
    "      });\n"
    "    }\n"
    "  }\n"
    "\n"
    "  // Array.prototype.at / String.prototype.at (ES2022). A negative index\n"
    "  // counts from the end; anything out of range is undefined rather than\n"
    "  // a wrapped value.\n"
    "  function at(index) {\n"
    "    var o = Object(this);\n"
    "    var len = o.length >>> 0;\n"
    "    var i = Math.trunc(+index) || 0;\n"
    "    if (i < 0) i += len;\n"
    "    return (i < 0 || i >= len) ? undefined : o[i];\n"
    "  }\n"
    "\n"
    "  def(Array.prototype, 'at', at);\n"
    "  def(String.prototype, 'at', at);\n"
    "\n"
    "  // Array.prototype.findLast / findLastIndex (ES2023).\n"
    "  def(Array.prototype, 'findLast', function (fn, thisArg) {\n"
    "    var o = Object(this);\n"
    "    for (var i = (o.length >>> 0) - 1; i >= 0; i--) {\n"
    "      if (fn.call(thisArg, o[i], i, o)) return o[i];\n"
    "    }\n"
    "    return undefined;\n"
    "  });\n"
    "\n"
    "  def(Array.prototype, 'findLastIndex', function (fn, thisArg) {\n"
    "    var o = Object(this);\n"
    "    for (var i = (o.length >>> 0) - 1; i >= 0; i--) {\n"
    "      if (fn.call(thisArg, o[i], i, o)) return i;\n"
    "    }\n"
    "    return -1;\n"
    "  });\n"
    "\n"
    "  // Object.hasOwn (ES2022).\n"
    "  def(Object, 'hasOwn', function (target, key) {\n"
    "    return Object.prototype.hasOwnProperty.call(Object(target), key);\n"
    "  });\n"
    "\n"
    "  // structuredClone. A deep copy covering the types that actually turn\n"
    "  // up: plain objects, arrays, Map, Set, Date, RegExp and typed arrays,\n"
    "  // with cycles preserved. It is not the full algorithm -- no transfer\n"
    "  // list, and a function throws, as the real one does.\n"
    "  def(globalThis, 'structuredClone', function (value) {\n"
    "    var seen = new Map();\n"
    "\n"
    "    function clone(v) {\n"
    "      if (v === null || typeof v !== 'object') {\n"
    "        if (typeof v === 'function') {\n"
    "          throw new TypeError('could not be cloned');\n"
    "        }\n"
    "        return v;\n"
    "      }\n"
    "      if (seen.has(v)) return seen.get(v);\n"
    "\n"
    "      var out;\n"
    "      if (v instanceof Date) return new Date(v.getTime());\n"
    "      if (v instanceof RegExp) return new RegExp(v.source, v.flags);\n"
    "      if (ArrayBuffer.isView(v)) return new v.constructor(v);\n"
    "      if (v instanceof ArrayBuffer) return v.slice(0);\n"
    "\n"
    "      if (v instanceof Map) {\n"
    "        out = new Map(); seen.set(v, out);\n"
    "        v.forEach(function (val, key) { out.set(clone(key), clone(val)); });\n"
    "        return out;\n"
    "      }\n"
    "      if (v instanceof Set) {\n"
    "        out = new Set(); seen.set(v, out);\n"
    "        v.forEach(function (val) { out.add(clone(val)); });\n"
    "        return out;\n"
    "      }\n"
    "      if (Array.isArray(v)) {\n"
    "        out = new Array(v.length); seen.set(v, out);\n"
    "        for (var i = 0; i < v.length; i++) out[i] = clone(v[i]);\n"
    "        return out;\n"
    "      }\n"
    "\n"
    "      out = {}; seen.set(v, out);\n"
    "      var keys = Object.keys(v);\n"
    "      for (var k = 0; k < keys.length; k++) out[keys[k]] = clone(v[keys[k]]);\n"
    "      return out;\n"
    "    }\n"
    "\n"
    "    return clone(value);\n"
    "  });\n"
    "})();\n";
}

} // namespace headless
