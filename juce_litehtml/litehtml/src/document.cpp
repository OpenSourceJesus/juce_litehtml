#include "html.h"
#include "document.h"
#include "stylesheet.h"
#include "html_tag.h"
#include "el_text.h"
#include "el_para.h"
#include "el_space.h"
#include "el_body.h"
#include "el_image.h"
#include "el_table.h"
#include "el_td.h"
#include "el_link.h"
#include "el_title.h"
#include "el_style.h"
#include "el_script.h"
#include "el_comment.h"
#include "el_cdata.h"
#include "el_base.h"
#include "el_anchor.h"
#include "el_break.h"
#include "el_div.h"
#include "el_font.h"
#include "el_tr.h"
#include "el_li.h"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <functional>
#include "gumbo.h"
#include "utf8_strings.h"

JSClassID litehtml::document::jsClassID = 0;

/* crust: the JS class finalizer, lifted out of a lambda in
   context.h's js_register_class template -- see the note there. Defined here,
   where document_js_object_ref is a complete type, so `delete` needs
   nothing deduced. */
void litehtml::document_js_finalize(JSRuntime*, JSValue val)
{
	/* crust: the pointer is declared on its own line and assigned after.
	   Declared straight from a cast the pass did not record a type for it,
	   so the delete had no destructor to resolve. */
	litehtml::document_js_object_ref* ref;
	ref = (litehtml::document_js_object_ref*)JS_GetOpaque (val, litehtml::document::jsClassID);
	if (ref != nullptr)
	{
		delete ref;
	}
}

/* crust: the constructor is defined inline in document.h. */

litehtml::document::~document()
{
	if (!JS_IsUninitialized(m_jsValue))
		JS_FreeValue (m_context->js_context(), m_jsValue);

	m_over_element = nullptr;

	if(m_container)
	{
		/* crust: spelled element type -- `auto` cannot deduce from a map
		   range, and the subset walks maps as `pair<K,V> *`. */
		for(const std::pair<tstring, font_item>& m_font : m_fonts)
		{
			m_container->delete_font(m_font.second.font);
		}
	}
}

//----------------------------------------------------------
// JavaScript interface methods

static std::shared_ptr<litehtml::document> js_get_document(JSContext* ctx, JSValueConst self)
{
	/* crust: `auto*` replaced by the written type. */
	litehtml::document_js_object_ref* ref { litehtml::context::js_get_object_ref<litehtml::document, litehtml::document_js_object_ref>(self) };
	if (ref != nullptr)
		return ref->document->shared_from_this();

	return nullptr;
}

static JSValue js_createElement(JSContext* ctx, JSValueConst self, int argc, JSValueConst* args)
{
	if (argc != 1)
		return JS_UNDEFINED;

	/* crust: `auto` written out -- the pass reads types as spelled. */
	std::shared_ptr<litehtml::document> document { js_get_document(ctx, self) };
	if (document)
	{
		/* crust: `auto` written out. */
		const char* tagName { JS_ToCString(ctx, args[0]) };
		/* crust: `auto` written out -- the pass reads types as spelled. */
		std::shared_ptr<litehtml::element> element { document->create_element(tagName, {}) };
		JS_FreeCString(ctx, tagName);

		if (element != nullptr)
		{
			document->stash_element(element);
			return JS_DupValue(ctx, *element->js_value());
		}
	}

	return JS_NULL;
}

static JSValue js_createTextNode(JSContext* ctx, JSValueConst self, int argc, JSValueConst* args)
{
	if (argc > 1)
		return JS_EXCEPTION;

	/* crust: `auto` written out -- the pass reads types as spelled. */
	std::shared_ptr<litehtml::document> document { js_get_document(ctx, self) };
	if (document)
	{
		litehtml::tchar_t* text { nullptr };
		std::shared_ptr<litehtml::element> textNode { nullptr };

		if (argc > 0)
		{
			/* crust: written type, and renamed off `text` -- the enclosing
			   function already declares a `tchar_t* text`, and the pass does
			   not scope locals by block, so the two collided. */
			const char* text_arg { JS_ToCString(ctx, args[0]) };
			textNode = document->create_text_node(text_arg);
			JS_FreeCString(ctx, text_arg);
		}
		else
		{
			textNode = document->create_text_node(_t(""));
		}

		document->stash_element(textNode);
		return JS_DupValue(ctx, *textNode->js_value());
	}

	return JS_NULL;
}

static JSValue js_getElementById(JSContext* ctx, JSValueConst self, int argc, JSValueConst* args)
{
	if (argc != 1)
		return JS_EXCEPTION;

	/* crust: `auto` written out -- the pass reads types as spelled. */
	std::shared_ptr<litehtml::document> document { js_get_document(ctx, self) };
	if (document)
	{
		std::shared_ptr<litehtml::element> element { nullptr };
		/* crust: written type, and the selector is built with append --
		   string `operator+` is not in the subset. */
		const char* id_arg { JS_ToCString(ctx, args[0]) };

		if (document->root())
		{
			litehtml::tstring id_sel;
			id_sel.push_back(_t('#'));
			id_sel.append(id_arg);
			/* crust: root() is a call result; virtual select_one_str needs a
			   named receiver, and the owning return has to land on a typed
			   local before the assignment. */
			std::shared_ptr<litehtml::element> root_el = document->root();
			std::shared_ptr<litehtml::element> found = root_el->select_one_str(id_sel);
			element = found;
		}

		JS_FreeCString(ctx, id_arg);

		if (element != nullptr)
			return JS_DupValue(ctx, *element->js_value());
	}

	return JS_NULL;
}

void litehtml::document::register_js_prototype(JSContext* ctx, JSValue prototype)
{
	litehtml::context::js_register_method(ctx, prototype, "createElement", js_createElement);
	litehtml::context::js_register_method(ctx, prototype, "createTextNode", js_createTextNode);
	litehtml::context::js_register_method(ctx, prototype, "getElementById", js_getElementById);
}

//----------------------------------------------------------

std::shared_ptr<litehtml::document> litehtml::document::createFromString( const tchar_t* str, litehtml::document_container* objPainter, litehtml::context* ctx, litehtml::css* user_styles)
{
	return createFromUTF8(litehtml_to_utf8(str), objPainter, ctx, user_styles);
}

std::shared_ptr<litehtml::document> litehtml::document::createFromUTF8(const char* str, litehtml::document_container* objPainter, litehtml::context* ctx, litehtml::css* user_styles)
{
	// parse document into GumboOutput
	GumboOutput* output = gumbo_parse((const char*) str);

	// Create litehtml::document
	std::shared_ptr<litehtml::document> doc = std::make_shared<litehtml::document>(objPainter, ctx);

	// Create litehtml::elements.
	elements_vector root_elements;
	doc->create_node(output->root, root_elements, true);
	if (!root_elements.empty())
	{
		doc->m_root = root_elements.back();
	}
	// Destroy GumboOutput
	gumbo_destroy_output(&kGumboDefaultOptions, output);

	// Let's process created elements tree
	if (doc->m_root)
	{
		/* crust: container() is a call result; virtual dispatch needs a
		   named receiver so the helper evaluates it once. */
		document_container* cont = doc->container();
		cont->get_media_features(doc->m_media);

		doc->m_root->set_pseudo_class(_t("root"), true);

		// apply master CSS
		doc->m_root->apply_stylesheet(*ctx->master_css());

		// parse elements attributes
		doc->m_root->parse_attributes();

		// parse style sheets linked in document
		std::shared_ptr<media_query_list> media;
		/* crust: indexed loop; see document.h on `m_css`. */
		for (int css_i = 0; css_i < (int)doc->m_css.size(); css_i++)
		{
			const css_text& css = doc->m_css[css_i];
			if (!css.media.empty())
			{
				media = media_query_list::create_from_string(css.media, doc);
			}
			else
			{
				media = nullptr;
			}
			doc->m_styles.parse_stylesheet(css.text.c_str(), css.baseurl.c_str(), doc, media);
		}
		// Sort css selectors using CSS rules.
		doc->m_styles.sort_selectors();

		// get current media features
		if (!doc->m_media_lists.empty())
		{
			doc->update_media_lists(doc->m_media);
		}

		// Apply parsed styles.
		doc->m_root->apply_stylesheet(doc->m_styles);

		// Apply user styles if any
		if (user_styles)
		{
			doc->m_root->apply_stylesheet(*user_styles);
		}

		// Parse applied styles in the elements
		doc->m_root->parse_styles();

		// Now the m_tabular_elements is filled with tabular elements.
		// We have to check the tabular elements for missing table elements
		// and create the anonymous boxes in visual table layout
		doc->fix_tables_layout();

		// Finally initialize elements
		doc->m_root->init();
	}

	return doc;
}

litehtml::uint_ptr litehtml::document::add_font( const tchar_t* name, int size, const tchar_t* weight, const tchar_t* style, const tchar_t* decoration, font_metrics* fm )
{
	uint_ptr ret = 0;

	if(!name || !t_strcasecmp(name, _t("inherit")))
	{
		name = m_container->get_default_font_name();
	}

	if(!size)
	{
		/* crust: named receiver; see media_changed. */
		document_container* cont = container();
		size = cont->get_default_font_size();
	}

	tchar_t strSize[20];
	t_itoa(size, strSize, 20, 10);

	/* crust: `+=` needs a named string; a `const tchar_t*` / array has no
	   address as one. `append` takes the pointer directly. */
	tstring key = name;
	key.append(_t(":"));
	key.append(strSize);
	key.append(_t(":"));
	key.append(weight);
	key.append(_t(":"));
	key.append(style);
	key.append(_t(":"));
	key.append(decoration);

	if(m_fonts.find(key) == m_fonts.end())
	{
		font_style fs = (font_style) value_index(style, font_style_strings, fontStyleNormal);
		int	fw = value_index(weight, font_weight_strings, -1);
		if(fw >= 0)
		{
			switch(fw)
			{
			case litehtml::fontWeightBold:
				fw = 700;
				break;
			case litehtml::fontWeightBolder:
				fw = 600;
				break;
			case litehtml::fontWeightLighter:
				fw = 300;
				break;
			default:
				fw = 400;
				break;
			}
		} else
		{
			fw = t_atoi(weight);
			if(fw < 100)
			{
				fw = 400;
			}
		}

		unsigned int decor = 0;

		if(decoration)
		{
			std::vector<tstring> tokens;
			split_string(decoration.c_str(), tokens, _t(" "));
			for(auto & token : tokens)
			{
				if(!t_strcasecmp(token.c_str(), _t("underline")))
				{
					decor |= font_decoration_underline;
				} else if(!t_strcasecmp(token.c_str(), _t("line-through")))
				{
					decor |= font_decoration_linethrough;
				} else if(!t_strcasecmp(token.c_str(), _t("overline")))
				{
					decor |= font_decoration_overline;
				}
			}
		}

		/* crust: aggregate initialization is not in the subset. Identical
		   either way: `font_metrics` has a default constructor that zeroes
		   itself and runs in both spellings, so only `font` was ever being
		   set by the braces. */
		font_item fi;
		fi.font = 0;

		fi.font = m_container->create_font(name, size, fw, fs, decor, &fi.metrics);
		m_fonts[key] = fi;
		ret = fi.font;
		if(fm)
		{
			*fm = fi.metrics;
		}
	}
	return ret;
}

litehtml::uint_ptr litehtml::document::get_font( const tchar_t* name, int size, const tchar_t* weight, const tchar_t* style, const tchar_t* decoration, font_metrics* fm )
{
	if(!name || !t_strcasecmp(name, _t("inherit")))
	{
		name = m_container->get_default_font_name();
	}

	if(!size)
	{
		size = m_container->get_default_font_size();
	}

	tchar_t strSize[20];
	t_itoa(size, strSize, 20, 10);

	/* crust: see add_font -- `append` rather than `+=` for c-strings. */
	tstring key = name;
	key.append(_t(":"));
	key.append(strSize);
	key.append(_t(":"));
	key.append(weight);
	key.append(_t(":"));
	key.append(style);
	key.append(_t(":"));
	key.append(decoration);

	auto el = m_fonts.find(key);

	if(el != m_fonts.end())
	{
		if(fm)
		{
			*fm = el->second.metrics;
		}
		return el->second.font;
	}
	return add_font(name, size, weight, style, decoration, fm);
}

int litehtml::document::render( int max_width, render_type rt )
{
	int ret = 0;
	if(m_root)
	{
		if(rt == render_fixed_only)
		{
			m_fixed_boxes.clear();
			m_root->render_positioned(rt);
		} else
		{
			ret = m_root->render(0, 0, max_width);
			if(m_root->fetch_positioned())
			{
				m_fixed_boxes.clear();
				m_root->render_positioned(rt);
			}
			m_size.width	= 0;
			m_size.height	= 0;
			m_root->calc_document_size(m_size);
		}
	}
	return ret;
}

void litehtml::document::draw( uint_ptr hdc, int x, int y, const position* clip )
{
	if(m_root)
	{
		m_root->draw(hdc, x, y, clip);
		m_root->draw_stacking_context(hdc, x, y, clip, true);
	}
}

int litehtml::document::cvt_units_str( const tchar_t* str, int fontSize, bool* is_percent/*= 0*/ ) const
{
	if(!str)	return 0;

	css_length val;
	val.fromString(str);
	if(is_percent && val.units() == css_units_percentage && !val.is_predefined())
	{
		*is_percent = true;
	}
	return cvt_units(val, fontSize);
}

int litehtml::document::cvt_units( css_length& val, int fontSize, int size ) const
{
	if(val.is_predefined())
	{
		return 0;
	}
	int ret;
	switch(val.units())
	{
	case css_units_percentage:
		ret = val.calc_percent(size);
		break;
	case css_units_em:
		ret = round_f(val.val() * (float) fontSize);
		val.set_value((float) ret, css_units_px);
		break;
	case css_units_pt:
		ret = m_container->pt_to_px((int) val.val());
		val.set_value((float) ret, css_units_px);
		break;
	case css_units_in:
		ret = m_container->pt_to_px((int) (val.val() * 72));
		val.set_value((float) ret, css_units_px);
		break;
	case css_units_cm:
		ret = m_container->pt_to_px((int) (val.val() * 0.3937 * 72));
		val.set_value((float) ret, css_units_px);
		break;
	case css_units_mm:
		ret = m_container->pt_to_px((int) (val.val() * 0.3937 * 72) / 10);
		val.set_value((float) ret, css_units_px);
		break;
	case css_units_vw:
		ret = (int)((double)m_media.width * (double)val.val() / 100.0);
		break;
	case css_units_vh:
		ret = (int)((double)m_media.height * (double)val.val() / 100.0);
		break;
	case css_units_vmin:
		ret = (int)((double)std::min(m_media.height, m_media.width) * (double)val.val() / 100.0);
		break;
	case css_units_vmax:
		ret = (int)((double)std::max(m_media.height, m_media.width) * (double)val.val() / 100.0);
		break;
	case css_units_rem:
		ret = (int) ((double) m_root->get_font_size() * (double) val.val());
		val.set_value((float) ret, css_units_px);
		break;
	default:
		ret = (int) val.val();
		break;
	}
	return ret;
}

int litehtml::document::width() const
{
	return m_size.width;
}

int litehtml::document::height() const
{
	return m_size.height;
}

void litehtml::document::add_stylesheet( const tchar_t* str, const tchar_t* baseurl, const tchar_t* media )
{
	if(str && str[0])
	{
		/* crust: bind the temporary before the reference parameter.
		   `push_back` takes `const T &`, and a call result has no address. */
		css_text ct(str, baseurl, media);
		m_css.push_back(ct);
	}
}

bool litehtml::document::on_mouse_over( int x, int y, int client_x, int client_y, std::vector<position>& redraw_boxes )
{
	if(!m_root)
	{
		return false;
	}

	std::shared_ptr<litehtml::element> over_el = m_root->get_element_by_point(x, y, client_x, client_y);

	bool state_was_changed = false;

	if(over_el != m_over_element)
	{
		if(m_over_element)
		{
			if(m_over_element->on_mouse_leave())
			{
				state_was_changed = true;
			}
		}
		m_over_element = over_el;
	}

	const tchar_t* cursor = nullptr;

	if(m_over_element)
	{
		if(m_over_element->on_mouse_over())
		{
			state_was_changed = true;
		}
		cursor = m_over_element->get_cursor();
	}

	m_container->set_cursor(cursor ? cursor : _t("auto"));

	if(state_was_changed)
	{
		return m_root->find_styles_changes(redraw_boxes, 0, 0);
	}
	return false;
}

bool litehtml::document::on_mouse_leave( std::vector<position>& redraw_boxes )
{
	if(!m_root)
	{
		return false;
	}
	if(m_over_element)
	{
		if(m_over_element->on_mouse_leave())
		{
			return m_root->find_styles_changes(redraw_boxes, 0, 0);
		}
	}
	return false;
}

bool litehtml::document::on_lbutton_down( int x, int y, int client_x, int client_y, std::vector<position>& redraw_boxes )
{
	if(!m_root)
	{
		return false;
	}

	std::shared_ptr<litehtml::element> over_el = m_root->get_element_by_point(x, y, client_x, client_y);

	bool state_was_changed = false;

	if(over_el != m_over_element)
	{
		if(m_over_element)
		{
			if(m_over_element->on_mouse_leave())
			{
				state_was_changed = true;
			}
		}
		m_over_element = over_el;
		if(m_over_element)
		{
			if(m_over_element->on_mouse_over())
			{
				state_was_changed = true;
			}
		}
	}

	const tchar_t* cursor = nullptr;

	if(m_over_element)
	{
		if(m_over_element->on_lbutton_down())
		{
			state_was_changed = true;
		}
		cursor = m_over_element->get_cursor();
	}

	m_container->set_cursor(cursor ? cursor : _t("auto"));

	if(state_was_changed)
	{
		return m_root->find_styles_changes(redraw_boxes, 0, 0);
	}

	return false;
}

bool litehtml::document::on_lbutton_up( int x, int y, int client_x, int client_y, std::vector<position>& redraw_boxes )
{
	if(!m_root)
	{
		return false;
	}
	if(m_over_element)
	{
		if(m_over_element->on_lbutton_up())
		{
			return m_root->find_styles_changes(redraw_boxes, 0, 0);
		}
	}
	return false;
}

std::shared_ptr<litehtml::element> litehtml::document::create_element(const tchar_t* tag_name, const string_map& attributes)
{
	std::shared_ptr<litehtml::element> newTag;
	std::shared_ptr<document> this_doc = shared_from_this();
	if(m_container)
	{
		newTag = m_container->create_element(tag_name, attributes, this_doc);
	}
	if(!newTag)
	{
		if(!t_strcmp(tag_name, _t("br")))
		{
			newTag = std::make_shared<litehtml::el_break>(this_doc);
		} else if(!t_strcmp(tag_name, _t("p")))
		{
			newTag = std::make_shared<litehtml::el_para>(this_doc);
		} else if(!t_strcmp(tag_name, _t("img")))
		{
			newTag = std::make_shared<litehtml::el_image>(this_doc);
		} else if(!t_strcmp(tag_name, _t("table")))
		{
			newTag = std::make_shared<litehtml::el_table>(this_doc);
		} else if(!t_strcmp(tag_name, _t("td")) || !t_strcmp(tag_name, _t("th")))
		{
			newTag = std::make_shared<litehtml::el_td>(this_doc);
		} else if(!t_strcmp(tag_name, _t("link")))
		{
			newTag = std::make_shared<litehtml::el_link>(this_doc);
		} else if(!t_strcmp(tag_name, _t("title")))
		{
			newTag = std::make_shared<litehtml::el_title>(this_doc);
		} else if(!t_strcmp(tag_name, _t("a")))
		{
			newTag = std::make_shared<litehtml::el_anchor>(this_doc);
		} else if(!t_strcmp(tag_name, _t("tr")))
		{
			newTag = std::make_shared<litehtml::el_tr>(this_doc);
		} else if(!t_strcmp(tag_name, _t("style")))
		{
			newTag = std::make_shared<litehtml::el_style>(this_doc);
		} else if(!t_strcmp(tag_name, _t("base")))
		{
			newTag = std::make_shared<litehtml::el_base>(this_doc);
		} else if(!t_strcmp(tag_name, _t("body")))
		{
			newTag = std::make_shared<litehtml::el_body>(this_doc);
		} else if(!t_strcmp(tag_name, _t("div")))
		{
			newTag = std::make_shared<litehtml::el_div>(this_doc);
		} else if(!t_strcmp(tag_name, _t("script")))
		{
			newTag = std::make_shared<litehtml::el_script>(this_doc);
		} else if(!t_strcmp(tag_name, _t("font")))
		{
			newTag = std::make_shared<litehtml::el_font>(this_doc);
		} else if(!t_strcmp(tag_name, _t("li")))
		{
			newTag = std::make_shared<litehtml::el_li>(this_doc);
		} else
		{
			newTag = std::make_shared<litehtml::html_tag>(this_doc);
		}
	}

	if(newTag)
	{
		newTag->set_tagName(tag_name);

		for (const auto & attribute : attributes)
		{
			newTag->set_attr(attribute.first.c_str(), attribute.second.c_str());
		}

		newTag->init_js_value();
	}

	return newTag;
}

void litehtml::document::get_fixed_boxes( std::vector<position>& fixed_boxes )
{
	fixed_boxes = m_fixed_boxes;
}

void litehtml::document::add_fixed_box( const position& pos )
{
	m_fixed_boxes.push_back(pos);
}

bool litehtml::document::media_changed()
{
	/* crust: named receiver; see create_node above. */
	document_container* cont = container();
	cont->get_media_features(m_media);
	if (update_media_lists(m_media))
	{
		m_root->refresh_styles();
		m_root->parse_styles();
		return true;
	}
	return false;
}

bool litehtml::document::lang_changed()
{
	if(!m_media_lists.empty())
	{
		tstring culture;
		/* crust: named receiver; see media_changed. */
		document_container* cont = container();
		cont->get_language(m_lang, culture);
		if(!culture.empty())
		{
			/* crust: string `operator+` is not in the subset; append. */
			m_culture = m_lang;
			m_culture.push_back(_t('-'));
			m_culture.append(culture.c_str());
		}
		else
		{
			m_culture.clear();
		}
		m_root->refresh_styles();
		m_root->parse_styles();
		return true;
	}
	return false;
}

bool litehtml::document::update_media_lists(const media_features& features)
{
	bool update_styles = false;
	for(auto & m_media_list : m_media_lists)
	{
		if(m_media_list->apply_media_features(features))
		{
			update_styles = true;
		}
	}
	return update_styles;
}

void litehtml::document::add_media_list( const std::shared_ptr<media_query_list>& list )
{
	if(list)
	{
		/* crust: indexed membership test -- `std::find` is a function
		   template and needs an explicit `<T>` the call does not have. */
		bool found = false;
		for (int i = 0; i < (int)m_media_lists.size(); i++)
		{
			if (m_media_lists[i] == list)
			{
				found = true;
				break;
			}
		}
		if(!found)
		{
			m_media_lists.push_back(list);
		}
	}
}

void litehtml::document::create_node(void* gnode, elements_vector& elements, bool parseTextNode)
{
	/* crust: `auto` written out -- the pass reads types as spelled, and a
	   cast expression has no spelling to deduce from. */
	GumboNode* node = (GumboNode*)gnode;
	switch (node->type)
	{
	case GUMBO_NODE_ELEMENT:
		{
			string_map attrs;
			GumboAttribute* attr;
			for (unsigned int i = 0; i < node->v.element.attributes.length; i++)
			{
				attr = (GumboAttribute*)node->v.element.attributes.data[i];
				/* crust: bind call results before map key/value refs. */
				tstring attr_name = litehtml_from_utf8(attr->name);
				tstring attr_value = litehtml_from_utf8(attr->value);
				attrs[attr_name] = attr_value;
			}


			std::shared_ptr<litehtml::element> ret;
			const char* tag = gumbo_normalized_tagname(node->v.element.tag);
			if (tag[0])
			{
				ret = create_element(litehtml_from_utf8(tag), attrs);
			}
			else
			{
				if (node->v.element.original_tag.data && node->v.element.original_tag.length)
				{
					std::string strA;
					gumbo_tag_from_original_text(&node->v.element.original_tag);
					strA.append(node->v.element.original_tag.data, node->v.element.original_tag.length);
					ret = create_element(litehtml_from_utf8(strA.c_str()), attrs);
				}
			}
			if (!strcmp(tag, "script"))
			{
				parseTextNode = false;
			}
			if (ret)
			{
				elements_vector child;
				for (unsigned int i = 0; i < node->v.element.children.length; i++)
				{
					child.clear();
					create_node(static_cast<GumboNode*> (node->v.element.children.data[i]), child, parseTextNode);
					/* crust: indexed loop rather than for_each with an
					   inline closure -- the subset inlines those at their
					   call sites, so each needs a name to be found by. */
					for (int ci = 0; ci < (int)child.size(); ci++)
					{
						ret->appendChild(child[ci]);
					}
				}
				elements.push_back(std::move(ret));
			}
		}
		break;
	case GUMBO_NODE_TEXT:
		{
			std::wstring str;
			std::wstring str_in = (const wchar_t*) (utf8_to_wchar(node->v.text.text));
			if (!parseTextNode)
			{
				/* crust: named local -- litehtml_from_wchar builds a
				   wchar_to_utf8 by value, and a method call on a by-value
				   result has no object to call it on. */
				litehtml::wchar_to_utf8 whole(str_in);
				/* crust: bind make_shared / shared_from_this before push_back. */
				std::shared_ptr<litehtml::element> self = shared_from_this();
				std::shared_ptr<litehtml::element> el = std::make_shared<el_text>(whole.c_str(), self);
				elements.push_back(el);
			}
			else
			{
				/* crust: was two capturing closures passed to split_text;
				   see html.h. The pieces come back in a vector instead. */
				string_vector parts;
				std::vector<int> kinds;
				m_container->split_text_parts(node->v.text.text, parts, kinds);
				for (int pi = 0; pi < (int)parts.size(); pi++)
				{
					/* crust: bind before push_back reference parameter. */
					std::shared_ptr<litehtml::element> self = shared_from_this();
					if (kinds[pi] == 0)
					{
						std::shared_ptr<litehtml::element> el = std::make_shared<el_text>(parts[pi].c_str(), self);
						elements.push_back(el);
					}
					else
					{
						std::shared_ptr<litehtml::element> el = std::make_shared<el_space>(parts[pi].c_str(), self);
						elements.push_back(el);
					}
				}
			}
		}
		break;
	case GUMBO_NODE_CDATA:
		{
			std::shared_ptr<litehtml::element> ret = std::make_shared<el_cdata>(shared_from_this());
			ret->set_data(litehtml_from_utf8(node->v.text.text));
			elements.push_back(ret);
		}
		break;
	case GUMBO_NODE_COMMENT:
		{
			std::shared_ptr<litehtml::element> ret = std::make_shared<el_comment>(shared_from_this());
			ret->set_data(litehtml_from_utf8(node->v.text.text));
			elements.push_back(ret);
		}
		break;
	case GUMBO_NODE_WHITESPACE:
		{
			tstring str = litehtml_from_utf8(node->v.text.text);
			for (size_t i = 0; i < str.length(); i++)
			{
				/* crust: substr returns a string by value; bind it first. */
				tstring one_char = str.substr(i, 1);
				/* crust: bind before push_back reference parameter. */
				std::shared_ptr<litehtml::element> self = shared_from_this();
				std::shared_ptr<litehtml::element> el = std::make_shared<el_space>(one_char.c_str(), self);
				elements.push_back(el);
			}
		}
		break;
	default:
		break;
	}
}

void litehtml::document::fix_tables_layout()
{
	size_t i = 0;
	while (i < m_tabular_elements.size())
	{
		std::shared_ptr<litehtml::element> el_ptr = m_tabular_elements[i];

		switch (el_ptr->get_display())
		{
		case display_inline_table:
		case display_table:
			fix_table_children(el_ptr, display_table_row_group, _t("table-row-group"));
			break;
		case display_table_footer_group:
		case display_table_row_group:
		case display_table_header_group:
			{
				std::shared_ptr<litehtml::element> parent = el_ptr->parent();
				if (parent)
				{
					if (parent->get_display() != display_inline_table)
						fix_table_parent(el_ptr, display_table, _t("table"));
				}
				fix_table_children(el_ptr, display_table_row, _t("table-row"));
			}
			break;
		case display_table_row:
			fix_table_parent(el_ptr, display_table_row_group, _t("table-row-group"));
			fix_table_children(el_ptr, display_table_cell, _t("table-cell"));
			break;
		case display_table_cell:
			fix_table_parent(el_ptr, display_table_row, _t("table-row"));
			break;
		// TODO: make table layout fix for table-caption, table-column etc. elements
		case display_table_caption:
		case display_table_column:
		case display_table_column_group:
		default:
			break;
		}
		i++;
	}
}

/* crust: this was an inline closure inside fix_table_children, which the
   subset cannot express: the binding had no written type, and the body walked
   the child list with iterators, which the supplied container does not have.
   Rewritten to build a fresh child list by index -- the same move already
   made in fix_table_parent below, and the same result. */
std::shared_ptr<litehtml::element> litehtml::document::make_anonymous_wrapper(std::shared_ptr<litehtml::element>& el_ptr, const tchar_t* disp_str, elements_vector& items)
{
	std::shared_ptr<litehtml::element> annon_tag = std::make_shared<html_tag>(shared_from_this());
	/* crust: string `operator+` is not in the subset; append. */
	tstring annon_style;
	annon_style.append(_t("display:"));
	annon_style.append(disp_str);
	annon_tag->add_style(annon_style, _t(""));
	annon_tag->parent(el_ptr);
	annon_tag->parse_styles();
	for (int ti = 0; ti < (int)items.size(); ti++)
	{
		annon_tag->appendChild(items[ti]);
	}
	return annon_tag;
}

void litehtml::document::fix_table_children(std::shared_ptr<litehtml::element>& el_ptr, style_display disp, const tchar_t* disp_str)
{
	elements_vector tmp;
	elements_vector out;

	for (int i = 0; i < (int)el_ptr->m_children.size(); i++)
	{
		std::shared_ptr<litehtml::element> child = el_ptr->m_children[i];

		if (child->get_display() != disp)
		{
			/* A run of children that do not have the wanted display is
			   gathered up and later reparented under one anonymous tag. */
			if (!child->is_table_skip() || !tmp.empty())
			{
				if (disp != display_table_row_group || child->get_display() != display_table_caption)
				{
					tmp.push_back(child);
					continue;
				}
			}
			out.push_back(child);
		}
		else
		{
			if (!tmp.empty())
			{
				/* crust: bind the call result before push_back's reference. */
				std::shared_ptr<litehtml::element> wrap = make_anonymous_wrapper(el_ptr, disp_str, tmp);
				out.push_back(wrap);
				tmp.clear();
			}
			out.push_back(child);
		}
	}

	if (!tmp.empty())
	{
		/* crust: bind the call result before push_back's reference. */
		std::shared_ptr<litehtml::element> wrap = make_anonymous_wrapper(el_ptr, disp_str, tmp);
		out.push_back(wrap);
		tmp.clear();
	}

	el_ptr->m_children.clear();
	for (int i = 0; i < (int)out.size(); i++)
	{
		el_ptr->m_children.push_back(out[i]);
	}
}

void litehtml::document::fix_table_parent(std::shared_ptr<litehtml::element>& el_ptr, style_display disp, const tchar_t* disp_str)
{
	std::shared_ptr<litehtml::element> parent = el_ptr->parent();

	if (parent->get_display() != disp)
	{
		/* crust: an index search rather than find_if with a lambda, and
		   indices rather than iterators throughout. The subset inlines a
		   lambda at its call sites so it needs one bound to a name, and it
		   has no iterator category -- an index says the same thing here and
		   compiles the same under either toolchain. */
		int this_index = -1;
		for (int k = 0; k < (int)parent->m_children.size(); k++)
		{
			if (parent->m_children[k] == el_ptr)
			{
				this_index = k;
				break;
			}
		}
		if (this_index >= 0)
		{
			style_display el_disp = el_ptr->get_display();
			int first = this_index;
			int last = this_index;
			int cur = this_index;

			// find first element with same display
			while (true)
			{
				if (cur == 0) break;
				cur--;
				if (parent->m_children[cur]->is_table_skip() || parent->m_children[cur]->get_display() == el_disp)
				{
					first = cur;
				}
				else
				{
					break;
				}
			}

			// find last element with same display
			cur = this_index;
			while (true)
			{
				cur++;
				if (cur >= (int)parent->m_children.size()) break;

				if (parent->m_children[cur]->is_table_skip() || parent->m_children[cur]->get_display() == el_disp)
				{
					last = cur;
				}
				else
				{
					break;
				}
			}

			// extract elements with the same display and wrap them with anonymous object
			std::shared_ptr<litehtml::element> annon_tag = std::make_shared<html_tag>(shared_from_this());
			/* crust: string `operator+` is not in the subset; append. */
			tstring annon_style;
			annon_style.append(_t("display:"));
			annon_style.append(disp_str);
			annon_tag->add_style(annon_style, _t(""));
			annon_tag->parent(parent);
			annon_tag->parse_styles();
			for (int ai = first; ai <= last; ai++)
			{
				annon_tag->appendChild(parent->m_children[ai]);
			}
			parent->m_children.erase(parent->m_children.begin() + first, parent->m_children.begin() + last + 1);
			parent->m_children.insert(parent->m_children.begin() + first, annon_tag);
		}
	}
}

void litehtml::document::append_children_from_string(element& parent, const tchar_t* str)
{
	append_children_from_utf8(parent, litehtml_to_utf8(str));
}

void litehtml::document::append_children_from_utf8(element& parent, const char* str)
{
	// parent must belong to this document
	/* crust: get_document() returns by value; bind it before .get(). */
	std::shared_ptr<litehtml::document> parent_doc = parent.get_document();
	if (parent_doc.get() != this)
	{
		return;
	}

	// parse document into GumboOutput
	GumboOutput* output = gumbo_parse((const char*) str);

	// Create litehtml::elements.
	elements_vector child_elements;
	create_node(output->root, child_elements, true);

	// Destroy GumboOutput
	gumbo_destroy_output(&kGumboDefaultOptions, output);

	// Let's process created elements tree
	for (const auto& child : child_elements)
	{
		// Add the child element to parent
		parent.appendChild(child);

		// apply master CSS
		child->apply_stylesheet(*m_context->master_css());

		// parse elements attributes
		child->parse_attributes();

		// Apply parsed styles.
		child->apply_stylesheet(m_styles);

		// Parse applied styles in the elements
		child->parse_styles();

		// Now the m_tabular_elements is filled with tabular elements.
		// We have to check the tabular elements for missing table elements
		// and create the anonymous boxes in visual table layout
		fix_tables_layout();

		// Fanaly initialize elements
		child->init();
	}
}

/* crust: see document.h -- el_text is constructed here, in a member, rather
   than from the file-scope JS callback above. */
std::shared_ptr<litehtml::element> litehtml::document::create_text_node(const tchar_t* text)
{
	return std::make_shared<el_text>(text ? text : _t(""), shared_from_this());
}

void litehtml::document::stash_element(std::shared_ptr<litehtml::element> el)
{
	m_stashed_elements.push_back(std::move(el));
}

void litehtml::document::remove_from_stash(std::shared_ptr<litehtml::element> el)
{
	auto it = m_stashed_elements.begin();

	while (it != m_stashed_elements.end())
	{
		if (it->get() == el.get())
			it = m_stashed_elements.erase(it);
		else
			++it;
	}
}
