#ifndef LH_DOCUMENT_H
#define LH_DOCUMENT_H

#include "style.h"
#include "types.h"
#include "context.h"

namespace litehtml
{
	struct css_text
	{
		typedef std::vector<css_text>	vector;

		tstring	text;
		tstring	baseurl;
		tstring	media;

		css_text() = default;

		css_text(const tchar_t* txt, const tchar_t* url, const tchar_t* media_str)
		{
			text	= txt ? txt : _t("");
			baseurl	= url ? url : _t("");
			media	= media_str ? media_str : _t("");
		}

		css_text(const css_text& val)
		{
			text	= val.text;
			baseurl	= val.baseurl;
			media	= val.media;
		}
	};

	class html_tag;

	/* crust: `document_js_object_ref` is declared in js_object_ref.h. */

	/* crust: JS class finalizer, defined in document.cpp. See context.h. */
	void document_js_finalize(JSRuntime* rt, JSValue val);

	class document : public std::enable_shared_from_this<document>
	{
	public:
		typedef std::shared_ptr<document>	ptr;
		typedef std::weak_ptr<document>		weak_ptr;

		static JSClassID jsClassID;

		static void register_js_prototype(JSContext* ctx, JSValue prototype);

	private:
		std::shared_ptr<element>			m_root;
		document_container*					m_container;
		fonts_map							m_fonts;
		/* crust: written out rather than `css_text::vector`. A class-scoped
		   typedef is left alone by the lowering, so the field's type never
		   resolved to a container and `m_css` was not walkable. */
		std::vector<css_text>					m_css;
		litehtml::css						m_styles;
		litehtml::web_color					m_def_color;
		litehtml::context*					m_context;
		litehtml::size						m_size;
		std::vector<position>					m_fixed_boxes;
		std::vector<std::shared_ptr<media_query_list> >			m_media_lists;
		std::shared_ptr<litehtml::element>						m_over_element;
		elements_vector						m_tabular_elements;
		media_features						m_media;
		tstring                             m_lang;
		tstring                             m_culture;

		JSValue								m_jsValue;

		std::vector<std::shared_ptr<litehtml::element>> m_stashed_elements;

	public:
		/* crust: defined inline. A constructor that is only declared in this
		   translation is not registered, so `make_shared<document>(..)` finds
		   no constructor to call. See html_tag.h. */
		document(litehtml::document_container* objContainer, litehtml::context* ctx)
		{
			m_container	= objContainer;
			m_context	= ctx;

			m_jsValue   = JS_NewObjectClass(ctx->js_context(), jsClassID);
			JS_SetOpaque (m_jsValue, new litehtml::document_js_object_ref(this));
		}
		virtual ~document();

		litehtml::document_container*	container()	{ return m_container; }
		litehtml::context*			    context() { return m_context; }
		/* crust: reference return -> pointer. */
		JSValue*						js_value() { return &m_jsValue; }
		/* crust: reference return -> pointer. */
		litehtml::css*					get_styles() { return &m_styles; }
		uint_ptr						get_font(const tchar_t* name, int size, const tchar_t* weight, const tchar_t* style, const tchar_t* decoration, font_metrics* fm);
		int								render(int max_width, render_type rt = render_all);
		void							draw(uint_ptr hdc, int x, int y, const position* clip);
		web_color						get_def_color()	{ return m_def_color; }
		/* crust: renamed from `cvt_units`. Overloads resolve by argument
		   count, and this took three like the css_length one below. */
		int								cvt_units_str(const tchar_t* str, int fontSize, bool* is_percent = nullptr) const;
		int								cvt_units(css_length& val, int fontSize, int size = 0) const;
		int								width() const;
		int								height() const;
		void							add_stylesheet(const tchar_t* str, const tchar_t* baseurl, const tchar_t* media);
		bool							on_mouse_over(int x, int y, int client_x, int client_y, std::vector<position>& redraw_boxes);
		bool							on_lbutton_down(int x, int y, int client_x, int client_y, std::vector<position>& redraw_boxes);
		bool							on_lbutton_up(int x, int y, int client_x, int client_y, std::vector<position>& redraw_boxes);
		bool							on_mouse_leave(std::vector<position>& redraw_boxes);
		std::shared_ptr<litehtml::element>			create_element(const tchar_t* tag_name, const string_map& attributes);
		std::shared_ptr<litehtml::element>					root();
		void							get_fixed_boxes(std::vector<position>& fixed_boxes);
		void							add_fixed_box(const position& pos);
		void							add_media_list(const std::shared_ptr<media_query_list>& list);
		bool							media_changed();
		bool							lang_changed();
		bool                            match_lang(const tstring & lang);
		void							add_tabular(const std::shared_ptr<litehtml::element>& el);
		element::const_ptr		        get_over_element() const { return m_over_element; }

		void                            append_children_from_string(element& parent, const tchar_t* str);
		void                            append_children_from_utf8(element& parent, const char* str);

		/* crust: `new el_text` from the file-scope JS callbacks was refused --
		   the class has to be one the lowering knows in the calling context,
		   and a qualified litehtml::el_text outside any litehtml member did
		   not resolve. Constructing it in a member instead is the shape that
		   already works everywhere else in the tree. */
		std::shared_ptr<litehtml::element>	create_text_node(const tchar_t* text);
		void							stash_element(std::shared_ptr<litehtml::element> el);
		void							remove_from_stash(std::shared_ptr<litehtml::element> el);

		static std::shared_ptr<litehtml::document> createFromString(const tchar_t* str, litehtml::document_container* objPainter, litehtml::context* ctx, litehtml::css* user_styles = nullptr);
		static std::shared_ptr<litehtml::document> createFromUTF8(const char* str, litehtml::document_container* objPainter, litehtml::context* ctx, litehtml::css* user_styles = nullptr);

	private:
		litehtml::uint_ptr	add_font(const tchar_t* name, int size, const tchar_t* weight, const tchar_t* style, const tchar_t* decoration, font_metrics* fm);

		void create_node(void* gnode, elements_vector& elements, bool parseTextNode);
		bool update_media_lists(const media_features& features);
		void fix_tables_layout();
		void fix_table_children(std::shared_ptr<litehtml::element>& el_ptr, style_display disp, const tchar_t* disp_str);
		/* crust: was a capturing lambda inside fix_table_children. */
		std::shared_ptr<litehtml::element> make_anonymous_wrapper(std::shared_ptr<litehtml::element>& el_ptr, const tchar_t* disp_str, elements_vector& items);
		void fix_table_parent(std::shared_ptr<litehtml::element>& el_ptr, style_display disp, const tchar_t* disp_str);
	};

	inline std::shared_ptr<litehtml::element> document::root()
	{
		return m_root;
	}
	inline void document::add_tabular(const std::shared_ptr<litehtml::element>& el)
	{
		m_tabular_elements.push_back(el);
	}
	inline bool document::match_lang(const tstring & lang)
	{
		return lang == m_lang || lang == m_culture;
	}
}

#endif  // LH_DOCUMENT_H
