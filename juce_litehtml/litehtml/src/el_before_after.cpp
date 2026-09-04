#include "html.h"
#include "el_before_after.h"
/* crust: the constructors are defined inline in el_before_after.h. */
#include "el_text.h"
#include "el_space.h"
#include "el_image.h"
#include "utf8_strings.h"

void litehtml::el_before_after_base::init_pseudo(bool before)
{
	if(before)
	{
		m_tag = _t("::before");
	} else
	{
        m_tag = _t("::after");
	}
}

void litehtml::el_before_after_base::add_style(const tstring& style, const tstring& baseurl)
{
	html_tag::add_style(style, baseurl);

	/* crust: `auto children = m_children` copy-constructs an owning vector
	   from a field two bases up (`this->_base._base.m_children`), which the
	   pass will not copy from an expression it cannot name. Copied element by
	   element instead, which is the same snapshot. */
	elements_vector children;
	for(int ci = 0; ci < (int)m_children.size(); ci++)
	{
		children.push_back(m_children[ci]);
	}
	m_children.clear();

	tstring content = get_style_property(_t("content"), false, _t(""));
	if(!content.empty())
	{
		int idx = value_index(content.c_str(), content_property_string);
		if(idx < 0)
		{
			tstring fnc;
			tstring::size_type i = 0;
			while(i < content.length() && i != tstring::npos)
			{
				if(content.at(i) == _t('"'))
				{
					fnc.clear();
					i++;
					tstring::size_type pos = content.find(_t('"'), i);
					tstring txt;
					if(pos == tstring::npos)
					{
						txt = content.substr(i);
						i = tstring::npos;
					} else
					{
						txt = content.substr(i, pos - i);
						i = pos + 1;
					}
					add_text(txt);
				} else if(content.at(i) == _t('('))
				{
					i++;
					litehtml::trim(fnc);
					litehtml::lcase(fnc);
					tstring::size_type pos = content.find(_t(')'), i);
					tstring params;
					if(pos == tstring::npos)
					{
						params = content.substr(i);
						i = tstring::npos;
					} else
					{
						params = content.substr(i, pos - i);
						i = pos + 1;
					}
					add_function(fnc, params);
					fnc.clear();
				} else
				{
					/* crust: `+=` needs a named string; `at` returns a char
					   with no address as one. `push_back` takes the char. */
					fnc.push_back(content.at(i));
					i++;
				}
			}
		}
	}

	if(m_children.empty())
	{
		/* crust: element-by-element, as above. */
		m_children.clear();
		for(int ci = 0; ci < (int)children.size(); ci++)
		{
			m_children.push_back(children[ci]);
		}
	}
}

void litehtml::el_before_after_base::add_text( const tstring& txt )
{
	tstring word;
	tstring esc;
	for(tstring::size_type i = 0; i < txt.length(); i++)
	{
		if( (txt.at(i) == _t(' ')) || (txt.at(i) == _t('\t')) || (txt.at(i) == _t('\\') && !esc.empty()) )
		{
			if(esc.empty())
			{
				if(!word.empty())
				{
					std::shared_ptr<litehtml::element> el = std::make_shared<el_text>(word.c_str(), get_document());
					appendChild(el);
					word.clear();
				}

				/* crust: substr returns a string by value, so there is no object
				   to call c_str on; bind it to a local first. */
				tstring one_char = txt.substr(i, 1);
				std::shared_ptr<litehtml::element> el = std::make_shared<el_space>(one_char.c_str(), get_document());
				appendChild(el);
			} else
			{
				/* crust: convert_escape returns by value; bind before append. */
				tstring escaped = convert_escape(esc.c_str() + 1);
				word.append(escaped.c_str());
				esc.clear();
				if(txt.at(i) == _t('\\'))
				{
					esc.push_back(txt.at(i));
				}
			}
		} else
		{
			if(!esc.empty() || txt.at(i) == _t('\\'))
			{
				esc.push_back(txt.at(i));
			} else
			{
				word.push_back(txt.at(i));
			}
		}
	}

	if(!esc.empty())
	{
		/* crust: see above -- bind convert_escape before append. */
		tstring escaped = convert_escape(esc.c_str() + 1);
		word.append(escaped.c_str());
	}
	if(!word.empty())
	{
		std::shared_ptr<litehtml::element> el = std::make_shared<el_text>(word.c_str(), get_document());
		appendChild(el);
		word.clear();
	}
}

void litehtml::el_before_after_base::add_function( const tstring& fnc, const tstring& params )
{
	int idx = value_index(fnc.c_str(), _t("attr;counter;url"));
	switch(idx)
	{
	// attr
	case 0:
		{
			tstring p_name = params;
			trim(p_name);
			lcase(p_name);
			std::shared_ptr<litehtml::element> el_parent = parent();
			if (el_parent)
			{
				const tchar_t* attr_value = el_parent->get_attr(p_name.c_str());
				if (attr_value)
				{
					add_text(attr_value);
				}
			}
		}
		break;
	// counter
	case 1:
		break;
	// url
	case 2:
		{
			tstring p_url = params;
			trim(p_url);
			if(!p_url.empty())
			{
				if(p_url.at(0) == _t('\'') || p_url.at(0) == _t('\"'))
				{
					p_url.erase(0, 1);
				}
			}
			if(!p_url.empty())
			{
				if(p_url.at(p_url.length() - 1) == _t('\'') || p_url.at(p_url.length() - 1) == _t('\"'))
				{
					p_url.erase(p_url.length() - 1, 1);
				}
			}
			if(!p_url.empty())
			{
				std::shared_ptr<litehtml::element> el = std::make_shared<el_image>(get_document());
				el->set_attr(_t("src"), p_url.c_str());
				el->set_attr(_t("style"), _t("display:inline-block"));
				el->set_tagName(_t("img"));
				appendChild(el);
				el->parse_attributes();
			}
		}
		break;
	}
}

litehtml::tstring litehtml::el_before_after_base::convert_escape( const tchar_t* txt )
{
    tchar_t* str_end;
	wchar_t u_str[2];
    u_str[0] = (wchar_t) t_strtol(txt, &str_end, 16);
    u_str[1] = 0;
	return litehtml::tstring(litehtml_from_wchar(u_str));
}

void litehtml::el_before_after_base::apply_stylesheet( const litehtml::css& stylesheet )
{

}
