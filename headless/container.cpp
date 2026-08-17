#include "container.h"
#include "el_script.h"
#include "imageinfo.h"
#include "strbuf.h"

#include <stdio.h>
#include <string.h>

namespace headless {

//==============================================================================
// Synthetic font metrics
//==============================================================================

/** Advance widths in 1/1000 em for a generic proportional sans face, indexed
    by ASCII code point. Chosen to resemble Helvetica so line breaking behaves
    plausibly; the exact values matter far less than the fact that they never
    change.
 */
static const short kAsciiWidths[128] = {
    // 0x00 - 0x1f (control characters, zero width)
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    // 0x20 - 0x2f   ! " # $ % & ' ( ) * + , - . /
    278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
    // 0x30 - 0x3f  0-9 : ; < = > ?
    556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
    // 0x40 - 0x4f  @ A-O
    1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
    // 0x50 - 0x5f  P-Z [ \ ] ^ _
    667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
    // 0x60 - 0x6f  ` a-o
    333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
    // 0x70 - 0x7f  p-z { | } ~ DEL
    556,556,333,500,278,556,500,722,500,500,500,334,260,334,584,0
};

/** Decodes one UTF-8 code point starting at *pos, advancing *pos past it.

    Takes a pointer rather than a reference to a pointer: the subset lowers a
    scalar reference to a pointer and dereferences its uses, and a reference
    *to* a pointer is a shape worth not relying on.
 */
static unsigned decodeCodepoint (const char* text, int* pos)
{
    int i = *pos;
    const unsigned char c = (unsigned char) text[i];

    if (c < 0x80)
    {
        *pos = i + 1;
        return (unsigned) c;
    }

    int extra = 0;
    unsigned cp = 0;

    if ((c & 0xe0) == 0xc0)      { extra = 1; cp = (unsigned) (c & 0x1f); }
    else if ((c & 0xf0) == 0xe0) { extra = 2; cp = (unsigned) (c & 0x0f); }
    else if ((c & 0xf8) == 0xf0) { extra = 3; cp = (unsigned) (c & 0x07); }
    else                         { *pos = i + 1; return (unsigned) c; }

    i = i + 1;
    int taken = 0;

    while (taken < extra && ((unsigned char) text[i] & 0xc0) == 0x80)
    {
        cp = (cp << 6) | (unsigned) ((unsigned char) text[i] & 0x3f);
        i = i + 1;
        taken = taken + 1;
    }

    *pos = i;
    return cp;
}

static int isWideCodepoint (unsigned cp)
{
    if (cp >= 0x1100 && cp <= 0x115f) return 1;     // Hangul Jamo
    if (cp >= 0x2e80 && cp <= 0xa4cf) return 1;     // CJK radicals .. Yi
    if (cp >= 0xac00 && cp <= 0xd7a3) return 1;     // Hangul syllables
    if (cp >= 0xf900 && cp <= 0xfaff) return 1;     // CJK compatibility
    if (cp >= 0xff00 && cp <= 0xff60) return 1;     // Fullwidth forms
    if (cp >= 0x20000 && cp <= 0x3fffd) return 1;
    return 0;
}

static void colorToString (litehtml::web_color color, std::string* out)
{
    out->push_back ('#');
    strAppendHex2 (out, (int) color.red);
    strAppendHex2 (out, (int) color.green);
    strAppendHex2 (out, (int) color.blue);
    strAppendHex2 (out, (int) color.alpha);
}

//==============================================================================

Font::Font()
{
    size = 16;
    weight = 400;
    italic = 0;
    decoration = 0;
    monospace = 0;
}

Font::~Font()
{
}

void Font::getMetrics (litehtml::font_metrics* fm)
{
    if (fm == 0)
        return;

    fm->ascent = (size * 800 + 500) / 1000;
    fm->descent = (size * 200 + 500) / 1000;
    fm->height = fm->ascent + fm->descent;
    fm->x_height = (size * 520 + 500) / 1000;
    fm->draw_spaces = (decoration != 0);
}

int Font::textWidth (const char* text)
{
    if (text == 0)
        return 0;

    // Bold faces run slightly wider; italics do not.
    int boldBonus = 0;

    if (weight >= 600)
        boldBonus = 40;

    long long milliEms = 0;
    int pos = 0;

    while (text[pos] != '\0')
    {
        const unsigned cp = decodeCodepoint (text, &pos);
        int w = 0;

        if (monospace != 0)
        {
            w = 600;
        }
        else if (cp < 128)
        {
            w = (int) kAsciiWidths[cp];

            if (w > 0)
                w = w + boldBonus;
        }
        else if (isWideCodepoint (cp) != 0)
        {
            w = 1000;
        }
        else
        {
            w = 500 + boldBonus;
        }

        milliEms = milliEms + (long long) w;
    }

    return (int) ((milliEms * (long long) size + 500) / 1000);
}

//==============================================================================

DrawCommand::DrawCommand()
{
    type = DrawTypeText;
    x = 0;
    y = 0;
    width = 0;
    height = 0;
    rgba = 0;
    font = 0;
}

ImageSize::ImageSize()
{
    width = 0;
    height = 0;
}

//==============================================================================

Container::Container()
{
    viewportW = 800;
    viewportH = 600;
    defaultFontName = "sans-serif";
    defaultFontSize = 16;
    defaultImageW = 0;
    defaultImageH = 0;
    fontsCreated = 0;
}

Container::~Container()
{
    int i = 0;

    while (i < (int) fonts.size())
    {
        Font* f = fonts[i];

        if (f != 0)
            delete f;

        i = i + 1;
    }

    fonts.clear();
}

void Container::setViewport (int w, int h) { viewportW = w; viewportH = h; }
int Container::getViewportWidth() { return viewportW; }
int Container::getViewportHeight() { return viewportH; }

void Container::setBaseDirectory (const char* dir)
{
    if (dir != 0)
        baseDir.assign (dir);
    else
        baseDir.clear();

    // A base directory is a document url whose path ends in a slash, so the
    // two spellings cannot drift apart.
    documentUrl.scheme = UrlSchemeFile;
    documentUrl.host.clear();
    documentUrl.port = 0;
    documentUrl.path.assign (baseDir.c_str());

    if (! documentUrl.path.empty())
        documentUrl.path.append ("/");
}

void Container::setDocumentUrl (Url* url)
{
    if (url == 0)
        return;

    documentUrl = *url;

    // Keep baseDir meaningful for a local document so nothing that still
    // reads it breaks.
    if (documentUrl.isRemote() == 0)
    {
        std::string dir;
        documentUrl.directory (&dir);

        baseDir.assign (dir.c_str());

        // directory() keeps the trailing slash; baseDir never had one.
        if (! baseDir.empty() && baseDir[baseDir.size() - 1] == '/')
        {
            std::string trimmed;
            int i = 0;

            while (i < (int) baseDir.size() - 1)
            {
                trimmed.push_back (baseDir[i]);
                i = i + 1;
            }

            baseDir = trimmed;
        }
    }
}

Url* Container::getDocumentUrl() { return &documentUrl; }
ResourceLoader* Container::getLoader() { return &loader; }

void Container::setDefaultImageSize (int w, int h)
{
    defaultImageW = w;
    defaultImageH = h;
}

void Container::setImageSize (const char* src, int w, int h)
{
    if (src == 0)
        return;

    ImageSize entry;
    entry.src.assign (src);
    entry.width = w;
    entry.height = h;

    imageSizes.push_back (entry);
}

void Container::setDefaultFontName (const char* name)
{
    if (name != 0)
        defaultFontName.assign (name);
}

void Container::setDefaultFontSize (int px) { defaultFontSize = px; }

std::ownvector<DrawCommand>* Container::getDrawCommands() { return &drawCommands; }
std::string* Container::getCaption() { return &caption; }
std::string* Container::getBaseUrl() { return &baseUrl; }
std::ownvector<std::string>* Container::getClickedAnchors() { return &clickedAnchors; }
std::ownvector<std::string>* Container::getRequestedImages() { return &requestedImages; }
std::ownvector<std::string>* Container::getRequestedScripts() { return &requestedScripts; }

int Container::getFontsCreated() { return fontsCreated; }
void Container::clearDrawCommands() { drawCommands.clear(); }

void Container::record (int type, int x, int y, int w, int h,
                        const char* text, litehtml::web_color color, Font* font)
{
    DrawCommand cmd;

    cmd.type = type;
    cmd.x = x;
    cmd.y = y;
    cmd.width = w;
    cmd.height = h;
    cmd.font = font;

    if (text != 0)
        cmd.text.assign (text);

    colorToString (color, &cmd.color);

    cmd.rgba = (((int) color.red) << 24) | (((int) color.green) << 16)
             | (((int) color.blue) << 8) | ((int) color.alpha);

    drawCommands.push_back (cmd);
}

//==============================================================================
// Fonts
//==============================================================================

litehtml::uint_ptr Container::create_font (const litehtml::tchar_t* faceName,
                                           int size,
                                           int weight,
                                           litehtml::font_style italic,
                                           unsigned int decoration,
                                           litehtml::font_metrics* fm)
{
    Font* font = new Font();

    // litehtml passes a comma-separated font stack; take the first entry and
    // strip quotes and surrounding whitespace.
    const char* stack = defaultFontName.c_str();

    if (faceName != 0)
        stack = faceName;

    std::string face;
    int i = 0;

    while (stack[i] != '\0' && stack[i] != ',')
    {
        const char c = stack[i];

        if (c != '"' && c != '\'')
            face.push_back (c);

        i = i + 1;
    }

    // Trim.
    std::string trimmed;
    int start = 0;
    int end = (int) face.size();

    while (start < end && (face[start] == ' ' || face[start] == '\t'))
        start = start + 1;

    while (end > start && (face[end - 1] == ' ' || face[end - 1] == '\t'))
        end = end - 1;

    while (start < end)
    {
        trimmed.push_back (face[start]);
        start = start + 1;
    }

    if (trimmed.empty())
        trimmed.assign (defaultFontName.c_str());

    font->face.assign (trimmed.c_str());
    font->size = size;
    font->weight = weight;
    font->italic = (italic == litehtml::fontStyleItalic) ? 1 : 0;
    font->decoration = (int) decoration;

    font->monospace = 0;

    if (strContainsNoCase (trimmed.c_str(), "mono") != 0
        || strContainsNoCase (trimmed.c_str(), "courier") != 0
        || strContainsNoCase (trimmed.c_str(), "consol") != 0)
    {
        font->monospace = 1;
    }

    font->getMetrics (fm);

    fonts.push_back (font);
    fontsCreated = fontsCreated + 1;

    return (litehtml::uint_ptr) font;
}

void Container::delete_font (litehtml::uint_ptr hFont)
{
    Font* font = (Font*) hFont;

    if (font == 0)
        return;

    int i = 0;

    while (i < (int) fonts.size())
    {
        if (fonts[i] == font)
        {
            // Compact rather than erase: the supplied vector has no erase.
            int j = i;

            while (j + 1 < (int) fonts.size())
            {
                fonts[j] = fonts[j + 1];
                j = j + 1;
            }

            fonts.pop_back();
            delete font;
            return;
        }

        i = i + 1;
    }
}

int Container::text_width (const litehtml::tchar_t* text, litehtml::uint_ptr hFont)
{
    Font* font = (Font*) hFont;

    if (font == 0)
        return 0;

    return font->textWidth (text);
}

int Container::pt_to_px (int pt) const
{
    // 96 dpi.
    return (pt * 96 + 36) / 72;
}

int Container::get_default_font_size() const
{
    return defaultFontSize;
}

const litehtml::tchar_t* Container::get_default_font_name() const
{
    return defaultFontName.c_str();
}

//==============================================================================
// Painting (recorded, never rasterised)
//==============================================================================

void Container::draw_text (litehtml::uint_ptr hdc,
                           const litehtml::tchar_t* text,
                           litehtml::uint_ptr hFont,
                           litehtml::web_color color,
                           const litehtml::position& pos)
{
    record (DrawTypeText, pos.x, pos.y, pos.width, pos.height,
            text, color, (Font*) hFont);
}

void Container::draw_background (litehtml::uint_ptr hdc, const litehtml::background_paint& bg)
{
    record (DrawTypeBackground, bg.border_box.x, bg.border_box.y,
            bg.border_box.width, bg.border_box.height,
            bg.image.c_str(), bg.color, 0);
}

void Container::draw_borders (litehtml::uint_ptr hdc,
                              const litehtml::borders& borders,
                              const litehtml::position& draw_pos,
                              bool root)
{
    record (DrawTypeBorders, draw_pos.x, draw_pos.y, draw_pos.width, draw_pos.height,
            0, borders.top.color, 0);
}

void Container::draw_list_marker (litehtml::uint_ptr hdc, const litehtml::list_marker& marker)
{
    record (DrawTypeListMarker, marker.pos.x, marker.pos.y,
            marker.pos.width, marker.pos.height,
            marker.image.c_str(), marker.color, 0);
}

void Container::set_clip (litehtml::uint_ptr hdc,
                          const litehtml::position& pos,
                          const litehtml::border_radiuses& bdr_radius,
                          bool valid_x, bool valid_y)
{
}

void Container::del_clip (litehtml::uint_ptr hdc)
{
}

//==============================================================================
// Images
//==============================================================================

void Container::load_image (const litehtml::tchar_t* src,
                            const litehtml::tchar_t* baseurl,
                            bool redraw_on_ready)
{
    if (src == 0)
        return;

    std::string s;
    s.assign (src);
    requestedImages.push_back (s);
}

void Container::get_image_size (const litehtml::tchar_t* src,
                                const litehtml::tchar_t* baseurl,
                                litehtml::size& sz)
{
    sz.width = defaultImageW;
    sz.height = defaultImageH;

    if (src == 0)
        return;

    // An explicitly registered size wins, so a test can pin one without a
    // file on disk.
    int i = 0;

    while (i < (int) imageSizes.size())
    {
        const ImageSize& entry = imageSizes[i];

        if (strcmp (entry.src.c_str(), src) == 0)
        {
            sz.width = entry.width;
            sz.height = entry.height;
            return;
        }

        i = i + 1;
    }

    // Otherwise read the real dimensions out of the file header. Layout needs
    // a size and nothing more, so no decoding happens here.
    // Read the real dimensions out of the header. Going through the loader
    // means a remote image is sized correctly too, and the cache stops the
    // bytes being fetched again when something draws it.
    std::string bytes;

    if (fetchImage (src, &bytes) != 0 && bytes.size() > 0)
    {
        int w = 0;
        int h = 0;

        if (imageSizeFromMemory ((const unsigned char*) bytes.c_str(),
                                 (int) bytes.size(), &w, &h) != 0
            && w > 0 && h > 0)
        {
            sz.width = w;
            sz.height = h;
        }
    }
}

//==============================================================================
// Document callbacks
//==============================================================================

void Container::set_caption (const litehtml::tchar_t* c)
{
    if (c != 0)
        caption.assign (c);
    else
        caption.clear();
}

void Container::set_base_url (const litehtml::tchar_t* url)
{
    if (url != 0)
        baseUrl.assign (url);
    else
        baseUrl.clear();
}

void Container::link (const std::shared_ptr<litehtml::document>& doc,
                      const std::shared_ptr<litehtml::element>& el)
{
}

void Container::on_anchor_click (const litehtml::tchar_t* url,
                                 const std::shared_ptr<litehtml::element>& el)
{
    if (url == 0)
        return;

    std::string s;
    s.assign (url);
    clickedAnchors.push_back (s);
}

void Container::set_cursor (const litehtml::tchar_t* c)
{
    if (c != 0)
        cursor.assign (c);
}

void Container::transform_text (litehtml::tstring& text, litehtml::text_transform tt)
{
    // ASCII only, which covers the test corpus.
    int i = 0;
    int atWordStart = 1;

    while (i < (int) text.size())
    {
        char c = text[i];

        if (tt == litehtml::text_transform_uppercase)
        {
            if (c >= 'a' && c <= 'z')
                text[i] = (char) (c - 32);
        }
        else if (tt == litehtml::text_transform_lowercase)
        {
            if (c >= 'A' && c <= 'Z')
                text[i] = (char) (c + 32);
        }
        else if (tt == litehtml::text_transform_capitalize)
        {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                atWordStart = 1;
            }
            else
            {
                if (atWordStart != 0 && c >= 'a' && c <= 'z')
                    text[i] = (char) (c - 32);

                atWordStart = 0;
            }
        }

        i = i + 1;
    }
}

int Container::resolveImagePath (const char* src, std::string* out)
{
    if (src == 0 || out == 0 || src[0] == '\0')
        return 0;

    Url resolved;

    if (resolveUrl (&documentUrl, src, &resolved) == 0)
        return 0;

    // Only something on disk has a path a front end can open.
    if (resolved.isRemote() != 0)
        return 0;

    out->assign (resolved.path.c_str());
    return 1;
}

int Container::fetchImage (const char* src, std::string* out)
{
    if (src == 0 || out == 0 || src[0] == '\0')
        return 0;

    Url resolved;

    if (resolveUrl (&documentUrl, src, &resolved) == 0)
        return 0;

    return loader.fetch (&resolved, out, 0);
}

int Container::readLocalFile (const char* url, std::string* out)
{
    if (url == 0 || out == 0 || url[0] == '\0')
        return 0;

    Url resolved;

    if (resolveUrl (&documentUrl, url, &resolved) == 0)
        return 0;

    // Goes through the loader, so a stylesheet or script referenced by a
    // remote document is fetched the same way the document was.
    return loader.fetch (&resolved, out, 0);
}

void Container::import_css (litehtml::tstring& text,
                            const litehtml::tstring& url,
                            litehtml::tstring& baseurl)
{
    baseurl = baseDir;
    text.clear();

    std::string content;

    if (readLocalFile (url.c_str(), &content) != 0)
        text = content;
}

void Container::import_script (litehtml::tstring& text, const litehtml::tstring& url)
{
    std::string requested;
    requested.assign (url.c_str());
    requestedScripts.push_back (requested);

    text.clear();

    std::string content;

    if (readLocalFile (url.c_str(), &content) != 0)
        text = content;
}

void Container::get_client_rect (litehtml::position& client) const
{
    client.x = 0;
    client.y = 0;
    client.width = viewportW;
    client.height = viewportH;
}

std::shared_ptr<litehtml::element> Container::create_element (const litehtml::tchar_t* tag_name,
                                                              const litehtml::string_map& attributes,
                                                              const std::shared_ptr<litehtml::document>& doc)
{
    // litehtml's own el_script cannot read its attributes (see el_script.h),
    // so substitute one that can. Everything else falls back to litehtml's
    // built-in element types.
    if (tag_name != 0 && strcmp (tag_name, "script") == 0)
        return std::make_shared<ScriptElement> (doc);

    return nullptr;
}

void Container::get_media_features (litehtml::media_features& media) const
{
    media.type = litehtml::media_type_screen;
    media.width = viewportW;
    media.height = viewportH;
    media.device_width = viewportW;
    media.device_height = viewportH;
    media.color = 8;
    media.monochrome = 0;
    media.color_index = 256;
    media.resolution = 96;
}

void Container::get_language (litehtml::tstring& language, litehtml::tstring& culture) const
{
    language = "en";
    culture = "";
}

} // namespace headless
