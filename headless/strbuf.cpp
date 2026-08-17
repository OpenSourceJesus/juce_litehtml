#include "strbuf.h"

namespace headless {

void strAppend (std::string* out, const char* text)
{
    if (out != 0 && text != 0)
        out->append (text);
}

void strAppendInt (std::string* out, int value)
{
    if (out == 0)
        return;

    if (value < 0)
    {
        out->push_back ('-');
        value = -value;
    }

    // Digits come out backwards, so buffer then reverse.
    char digits[16];
    int n = 0;

    if (value == 0)
    {
        digits[0] = '0';
        n = 1;
    }
    else
    {
        while (value > 0 && n < 15)
        {
            digits[n] = (char) ('0' + (value % 10));
            value = value / 10;
            n = n + 1;
        }
    }

    int i = n - 1;

    while (i >= 0)
    {
        out->push_back (digits[i]);
        i = i - 1;
    }
}

void strAppendHex2 (std::string* out, int value)
{
    if (out == 0)
        return;

    const char* hex = "0123456789abcdef";

    out->push_back (hex[(value >> 4) & 15]);
    out->push_back (hex[value & 15]);
}

void strAppendSpaces (std::string* out, int count)
{
    if (out == 0)
        return;

    int i = 0;

    while (i < count)
    {
        out->push_back (' ');
        i = i + 1;
    }
}

void strAppendSummary (std::string* out, const char* in, int maxChars)
{
    if (out == 0 || in == 0)
        return;

    // Collapse whitespace into a temporary first, so trimming the trailing
    // space does not have to reach back into the caller's buffer.
    std::string tmp;
    int lastWasSpace = 1;
    int i = 0;

    while (in[i] != '\0')
    {
        const char c = in[i];
        const int isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r') ? 1 : 0;

        if (isSpace != 0)
        {
            if (lastWasSpace == 0)
                tmp.push_back (' ');

            lastWasSpace = 1;
        }
        else
        {
            tmp.push_back (c);
            lastWasSpace = 0;
        }

        i = i + 1;
    }

    int len = (int) tmp.size();

    while (len > 0 && tmp[len - 1] == ' ')
        len = len - 1;

    if (len > maxChars)
    {
        // Do not cut a multi-byte character in half.
        int cut = maxChars;

        while (cut > 0 && ((tmp[cut] & 0xc0) == 0x80))
            cut = cut - 1;

        int j = 0;

        while (j < cut)
        {
            out->push_back (tmp[j]);
            j = j + 1;
        }

        out->append ("...");
        return;
    }

    int k = 0;

    while (k < len)
    {
        out->push_back (tmp[k]);
        k = k + 1;
    }
}

int strContainsNoCase (const char* haystack, const char* needle)
{
    if (haystack == 0 || needle == 0)
        return 0;

    int i = 0;

    while (haystack[i] != '\0')
    {
        int j = 0;

        while (needle[j] != '\0')
        {
            char a = haystack[i + j];
            char b = needle[j];

            if (a >= 'A' && a <= 'Z')
                a = (char) (a + 32);

            if (b >= 'A' && b <= 'Z')
                b = (char) (b + 32);

            if (a != b)
                break;

            j = j + 1;
        }

        if (needle[j] == '\0')
            return 1;

        i = i + 1;
    }

    return 0;
}

} // namespace headless
