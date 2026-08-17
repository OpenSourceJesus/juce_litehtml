#pragma once

#include "crust_compat.h"

namespace headless {

/** Reads the pixel dimensions of an image file from its header.

    litehtml needs a size before it can lay an <img> out, and a size is all it
    needs -- the pixels only matter to whatever eventually draws. So this
    parses headers rather than decoding, which keeps the base container free
    of any image library and gives every front end correct layout for free.

    Returns 1 and fills w/h on success, 0 when the format is unrecognised or
    the file cannot be read.

    Handles PNG, GIF, JPEG and BMP.
 */
int imageSizeFromFile (const char* path, int* w, int* h);

/** Same, from a buffer already in memory. */
int imageSizeFromMemory (const unsigned char* data, int length, int* w, int* h);

} // namespace headless
