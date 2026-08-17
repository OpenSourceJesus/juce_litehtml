#pragma once

#include "crust_compat.h"

namespace headless {

// The subset has no stream operators, so text is built by appending to a
// std::string. These are the few pieces ostringstream was doing.

/** Appends a C string. */
void strAppend (std::string* out, const char* text);

/** Appends a decimal integer. */
void strAppendInt (std::string* out, int value);

/** Appends a byte as two lowercase hex digits. */
void strAppendHex2 (std::string* out, int value);

/** Appends the given number of spaces. */
void strAppendSpaces (std::string* out, int count);

/** Appends `in` with runs of whitespace collapsed to one space, trimmed, and
    truncated to maxChars. Used to keep dump output on one line.
 */
void strAppendSummary (std::string* out, const char* in, int maxChars);

/** Case-insensitive substring test, ASCII only. */
int strContainsNoCase (const char* haystack, const char* needle);

} // namespace headless
