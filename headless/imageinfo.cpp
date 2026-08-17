#include "imageinfo.h"

#include <stdio.h>
#include <string.h>

namespace headless {

static int readBE16 (const unsigned char* p)
{
    return (((int) p[0]) << 8) | ((int) p[1]);
}

static int readBE32 (const unsigned char* p)
{
    return (((int) p[0]) << 24) | (((int) p[1]) << 16)
         | (((int) p[2]) << 8) | ((int) p[3]);
}

static int readLE16 (const unsigned char* p)
{
    return (((int) p[1]) << 8) | ((int) p[0]);
}

static int readLE32 (const unsigned char* p)
{
    return (((int) p[3]) << 24) | (((int) p[2]) << 16)
         | (((int) p[1]) << 8) | ((int) p[0]);
}

int imageSizeFromMemory (const unsigned char* data, int length, int* w, int* h)
{
    if (data == 0 || w == 0 || h == 0)
        return 0;

    // PNG: 8 byte signature, then an IHDR chunk whose payload starts with
    // width and height as big-endian 32 bit values.
    if (length >= 24
        && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
    {
        *w = readBE32 (data + 16);
        *h = readBE32 (data + 20);
        return 1;
    }

    // GIF: "GIF87a" or "GIF89a", then the logical screen size, little endian.
    if (length >= 10 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F')
    {
        *w = readLE16 (data + 6);
        *h = readLE16 (data + 8);
        return 1;
    }

    // BMP: "BM", then a DIB header with the size at a fixed offset.
    if (length >= 26 && data[0] == 'B' && data[1] == 'M')
    {
        *w = readLE32 (data + 18);
        *h = readLE32 (data + 22);

        // Height is signed and negative for a top-down bitmap.
        if (*h < 0)
            *h = -(*h);

        return 1;
    }

    // JPEG: walk the marker segments until a start-of-frame carries the size.
    if (length >= 4 && data[0] == 0xff && data[1] == 0xd8)
    {
        int pos = 2;

        while (pos + 9 < length)
        {
            if (data[pos] != 0xff)
            {
                // Not on a marker boundary; resynchronise rather than give up,
                // since fill bytes are legal between segments.
                pos = pos + 1;
                continue;
            }

            const int marker = (int) data[pos + 1];

            // Standalone markers carry no length.
            if (marker == 0xd8 || marker == 0x01
                || (marker >= 0xd0 && marker <= 0xd7))
            {
                pos = pos + 2;
                continue;
            }

            if (marker == 0xd9 || marker == 0xda)
                break;                      // end of image, or scan data

            const int segLength = readBE16 (data + pos + 2);

            if (segLength < 2)
                break;

            // SOF0..SOF15, excluding the non-frame markers in that range.
            const int isFrame =
                ((marker >= 0xc0 && marker <= 0xcf)
                 && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) ? 1 : 0;

            if (isFrame != 0)
            {
                if (pos + 9 >= length)
                    break;

                // Precision byte, then height then width, big endian.
                *h = readBE16 (data + pos + 5);
                *w = readBE16 (data + pos + 7);
                return 1;
            }

            pos = pos + 2 + segLength;
        }
    }

    return 0;
}

int imageSizeFromFile (const char* path, int* w, int* h)
{
    if (path == 0 || w == 0 || h == 0)
        return 0;

    FILE* f = fopen (path, "rb");

    if (f == 0)
        return 0;

    // A JPEG can carry a lot of metadata before its frame header, so read a
    // generous prefix rather than just the first few bytes.
    unsigned char buffer[65536];
    const size_t n = fread (buffer, 1, 65536, f);
    fclose (f);

    if (n == 0)
        return 0;

    return imageSizeFromMemory (buffer, (int) n, w, h);
}

} // namespace headless
