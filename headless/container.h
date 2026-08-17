#pragma once

#include "crust_compat.h"
#include "litehtml.h"
#include "loader.h"
#include "url.h"

namespace headless {

// Plain enum rather than `enum class`: the subset has no scoped enumeration.
enum DrawType {
    DrawTypeText = 0,
    DrawTypeBackground = 1,
    DrawTypeBorders = 2,
    DrawTypeListMarker = 3
};

/** Synthetic font.

    The headless container has no access to real font files, so text metrics
    come from a fixed width table. This keeps layout output identical on every
    machine, which is what makes it useful for testing.
 */
class Font
{
public:
    std::string face;
    int size;
    int weight;
    int italic;
    int decoration;
    int monospace;

    Font();

    // Virtual so a front end can subclass this with a real platform font
    // handle and still be destroyed through the base pointer the container
    // stores. See cairo/cairo_container.h.
    virtual ~Font();

    /** Fills in litehtml's metrics for this font. */
    virtual void getMetrics (litehtml::font_metrics* fm);

    /** Advance width of a UTF-8 string, in pixels. */
    virtual int textWidth (const char* text);
};

/** A single recorded paint operation. */
class DrawCommand
{
public:
    int type;
    int x;
    int y;
    int width;
    int height;
    std::string text;       // text content, or image url
    std::string color;      // "#rrggbbaa", for dumps
    int rgba;               // the same colour packed 0xRRGGBBAA, for backends
    Font* font;             // borrowed; the container owns it

    DrawCommand();
};

/** An explicit image size registered by the caller. */
class ImageSize
{
public:
    std::string src;
    int width;
    int height;

    ImageSize();
};

/** Headless implementation of litehtml::document_container.

    Nothing is rasterised. Paint calls are recorded into a display list which
    can then be dumped as text or inspected by tests.

    A front end that draws for real subclasses this and overrides the font and
    paint methods. Everything else -- image sizing, css and script loading,
    media features -- is inherited unchanged.
 */
class Container : public litehtml::document_container
{
public:
    Container();
    virtual ~Container();

    //== Configuration =========================================================

    void setViewport (int w, int h);
    int getViewportWidth();
    int getViewportHeight();

    /** Directory used to resolve relative urls for @import and <link>.
        Kept for callers that only ever deal in local files; it is the same
        thing as setDocumentUrl with a file path.
     */
    void setBaseDirectory (const char* dir);

    /** The url the document itself came from. Relative references resolve
        against this, so it must be set for a document fetched over http.
     */
    void setDocumentUrl (Url* url);
    Url* getDocumentUrl();

    /** The loader used for stylesheets, scripts and images. Network access is
        off by default; turn it on here.
     */
    ResourceLoader* getLoader();

    /** Size reported for images with no registered size. */
    void setDefaultImageSize (int w, int h);

    /** Register an explicit size for one image src. Overrides what would be
        read from the file, which is what tests use to pin a size.
     */
    void setImageSize (const char* src, int w, int h);

    /** Resolves an image src to a local path against the document url.
        Returns 1 only for something on disk -- a front end that draws images
        reads from a path, and a remote image has none. Use fetchImage for
        those.
     */
    int resolveImagePath (const char* src, std::string* out);

    /** Fetches an image's bytes, local or remote, through the cache. */
    int fetchImage (const char* src, std::string* out);

    void setDefaultFontName (const char* name);
    void setDefaultFontSize (int px);

    //== Results ===============================================================

    // Pointers rather than references: the subset refuses a reference return,
    // since lowering it to a pointer would change what assignment through the
    // result means at every call site.
    std::ownvector<DrawCommand>* getDrawCommands();
    std::string* getCaption();
    std::string* getBaseUrl();
    std::ownvector<std::string>* getClickedAnchors();
    std::ownvector<std::string>* getRequestedImages();
    std::ownvector<std::string>* getRequestedScripts();

    int getFontsCreated();
    void clearDrawCommands();

    /** Records one paint operation. A subclass that draws for real calls this
        too, so the display list stays available for testing.
     */
    void record (int type, int x, int y, int w, int h,
                 const char* text, litehtml::web_color color, Font* font);

    //== litehtml::document_container ==========================================

    virtual litehtml::uint_ptr create_font (const litehtml::tchar_t* faceName,
                                            int size,
                                            int weight,
                                            litehtml::font_style italic,
                                            unsigned int decoration,
                                            litehtml::font_metrics* fm);
    virtual void delete_font (litehtml::uint_ptr hFont);
    virtual int text_width (const litehtml::tchar_t* text, litehtml::uint_ptr hFont);
    virtual void draw_text (litehtml::uint_ptr hdc,
                            const litehtml::tchar_t* text,
                            litehtml::uint_ptr hFont,
                            litehtml::web_color color,
                            const litehtml::position& pos);
    virtual int pt_to_px (int pt) const;
    virtual int get_default_font_size() const;
    virtual const litehtml::tchar_t* get_default_font_name() const;
    virtual void draw_list_marker (litehtml::uint_ptr hdc, const litehtml::list_marker& marker);
    virtual void load_image (const litehtml::tchar_t* src,
                             const litehtml::tchar_t* baseurl,
                             bool redraw_on_ready);
    virtual void get_image_size (const litehtml::tchar_t* src,
                                 const litehtml::tchar_t* baseurl,
                                 litehtml::size& sz);
    virtual void draw_background (litehtml::uint_ptr hdc, const litehtml::background_paint& bg);
    virtual void draw_borders (litehtml::uint_ptr hdc,
                               const litehtml::borders& borders,
                               const litehtml::position& draw_pos,
                               bool root);

    virtual void set_caption (const litehtml::tchar_t* caption);
    virtual void set_base_url (const litehtml::tchar_t* base_url);
    virtual void link (const std::shared_ptr<litehtml::document>& doc,
                       const std::shared_ptr<litehtml::element>& el);
    virtual void on_anchor_click (const litehtml::tchar_t* url,
                                  const std::shared_ptr<litehtml::element>& el);
    virtual void set_cursor (const litehtml::tchar_t* cursor);
    virtual void transform_text (litehtml::tstring& text, litehtml::text_transform tt);
    virtual void import_css (litehtml::tstring& text,
                             const litehtml::tstring& url,
                             litehtml::tstring& baseurl);
    virtual void import_script (litehtml::tstring& text, const litehtml::tstring& url);
    virtual void set_clip (litehtml::uint_ptr hdc,
                           const litehtml::position& pos,
                           const litehtml::border_radiuses& bdr_radius,
                           bool valid_x, bool valid_y);
    virtual void del_clip (litehtml::uint_ptr hdc);
    virtual void get_client_rect (litehtml::position& client) const;
    virtual std::shared_ptr<litehtml::element> create_element (const litehtml::tchar_t* tag_name,
                                                               const litehtml::string_map& attributes,
                                                               const std::shared_ptr<litehtml::document>& doc);
    virtual void get_media_features (litehtml::media_features& media) const;
    virtual void get_language (litehtml::tstring& language, litehtml::tstring& culture) const;

protected:
    /** Reads a local file, resolving relative paths against the base
        directory. Returns 1 on success.
     */
    int readLocalFile (const char* url, std::string* out);

    int viewportW;
    int viewportH;

    std::string baseDir;
    Url documentUrl;
    ResourceLoader loader;
    std::string defaultFontName;
    int defaultFontSize;

    int defaultImageW;
    int defaultImageH;
    std::ownvector<ImageSize> imageSizes;

    std::vector<Font*> fonts;
    int fontsCreated;

    std::ownvector<DrawCommand> drawCommands;

    std::string caption;
    std::string baseUrl;
    std::string cursor;
    std::ownvector<std::string> clickedAnchors;
    std::ownvector<std::string> requestedImages;
    std::ownvector<std::string> requestedScripts;
};

} // namespace headless
